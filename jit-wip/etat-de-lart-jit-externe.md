# Okapia — Audit et pistes d'optimisation du JIT 680x0 → ARM64

**Projet :** Okapia — Basilisk II / System 7 sur Raspberry Pi 4 avec Circle bare metal  
**Architecture :** Motorola 68040 émulé → ARM64 Cortex-A72  
**JIT étudié :** `khawkins98/macemu-jit`  
**Date :** 14 novembre 2026  
**Statut :** JIT fonctionnel, mais sans accélération significative

---

## 1. Contexte et problème constaté

### 1.1 Environnement

Okapia repose sur :

- Raspberry Pi 4, processeur ARM Cortex-A72.
- Circle en bare metal, sans Linux.
- Basilisk II pour l'émulation Macintosh 68k.
- System 7 comme système invité.
- Émulation CPU sur un seul cœur, sans multithreading du moteur 68k.
- Backend JIT ARM64 expérimental issu de `macemu-jit`.

### 1.2 Observations

Le JIT a été intégré et fonctionne, mais les résultats sont décevants.

| Observation | État |
|---|---|
| Compilation 68k → ARM64 | Fonctionnelle |
| Exécution du code ARM64 généré | Fonctionnelle |
| Réutilisation de blocs compilés | Observée |
| Longueur habituelle des blocs | 2 à 5 instructions 68k |
| Speedometer | Performances similaires à l'interpréteur |
| Fluidité | Ralentissements ponctuels importants |
| Taille des caches | Différentes valeurs essayées |
| Options booléennes du JIT | Plusieurs combinaisons testées |
| Corrections du moteur | Plusieurs correctifs déjà appliqués |
| Gain de performances significatif | Non observé |

Mesures antérieures indépendantes, réalisées pendant un transfert SMB avec Mamba :

- Environ 27–32 millions d'instructions 68k émulées/s.
- Jusqu'à 35 millions/s observés.
- CPU fortement sollicité, autour de 85 %.
- Débit réseau de 2,2–2,5 Mo/s.

Ces mesures ne constituent pas un benchmark JIT/interpréteur. Elles servent de référence pour de futurs tests applicatifs.

### 1.3 Diagnostic initial

Le problème n'est probablement plus simplement lié à la taille du cache de code traduit.

Les principaux suspects sont :

1. Des unités de traduction trop courtes.
2. Un coût fixe d'entrée et de sortie de bloc excessif.
3. Des transitions insuffisamment directes entre blocs.
4. Des instructions qui retombent dans l'interpréteur.
5. Des invalidations et vérifications de checksums trop fréquentes.
6. Un code ARM64 généré trop coûteux.
7. Des synchronisations excessives du PC, des registres et des flags.
8. Des mécanismes de compatibilité ou de diagnostic encore actifs.

**Deux phénomènes sont à étudier séparément :**

- **Faible performance moyenne :** probablement liée au rendement des blocs natifs, aux fallbacks ou au dispatch.
- **Ralentissements ponctuels :** potentiellement liés aux compilations, invalidations, checksums, vidages de cache, interruptions ou sorties de diagnostic.

Ces hypothèses ne sont pas encore démontrées.

---

## 2. Priorité P0 — Instrumentation et établissement d'une référence

Avant toute nouvelle optimisation, rendre le moteur mesurable.

### 2.1 Reproductibilité

Conserver une configuration de référence documentée :

- Commit exact du backend JIT.
- Version et paramètres de Circle.
- Fréquence CPU et état thermique du Raspberry Pi.
- Taille de RAM Macintosh.
- Version de System 7.
- Modèle CPU émulé et mode de compatibilité.
- Options de compilation C/C++.
- Options JIT effectives.
- Taille du cache.
- État des fonctionnalités de diagnostic.
- Test Speedometer et sous-test exécuté.

Comparer systématiquement les résultats après redémarrage, avec plusieurs exécutions.

### 2.2 Instrumentation minimale

Ajouter des compteurs 64 bits en mémoire, sans affichage continu pendant les benchmarks.

| Métrique | Objectif |
|---|---|
| Nombre d'entrées dans le JIT | Quantifier les transitions |
| Nombre de blocs natifs exécutés | Mesurer l'utilisation réelle du JIT |
| Instructions 68k exécutées nativement | Calculer la couverture native |
| Instructions 68k exécutées par interpréteur | Mesurer les fallbacks |
| Nombre de compilations | Quantifier le travail de traduction |
| Nombre de recompilations | Détecter le churn |
| Cache hits / cache misses | Mesurer la réutilisation |
| Checksums réussis / échoués | Mesurer la validation |
| Invalidations ciblées / globales | Identifier les perturbations |
| Sorties vers le dispatcher C | Mesurer le coût des transitions |
| Chaînages directs entre blocs | Vérifier le chemin rapide |
| Temps CPU de compilation | Mesurer le coût de génération |
| Temps CPU d'exécution native | Mesurer le travail utile |
| Taille totale de code ARM64 généré | Évaluer l'expansion du code |
| Causes de fin de bloc | Comprendre la longueur 2–5 |

Distinguer les statistiques de **compilation des blocs** de celles de leur **exécution réelle**.

Un bloc compilé une fois et exécuté un million de fois doit avoir un poids approprié dans les statistiques.

### 2.3 Histogrammes indispensables

Mesurer la distribution des tailles de blocs :

| Taille | Blocs compilés | Exécutions | Instructions 68k exécutées |
|---|---:|---:|---:|
| 1 instruction | | | |
| 2 instructions | | | |
| 3–5 instructions | | | |
| 6–10 instructions | | | |
| 11–32 instructions | | | |
| 33–64 instructions | | | |
| Plus de 64 | | | |

Il faut également mesurer la longueur des séquences natives exécutées **sans retour au C**, car plusieurs petits blocs peuvent être efficacement chaînés.

### 2.4 Attention à l'instrumentation

- Aucun `printf` par instruction ou par bloc.
- Pas de sortie UART synchrone pendant les mesures.
- Préférer des compteurs simples ou un buffer circulaire préalloué.
- Récupérer les résultats après le benchmark.
- Prévoir un build de performance sans instrumentation.
- Mesurer le surcoût de l'instrumentation elle-même.

Le fork comporte notamment des traces dans `flush_icache_hard()` et sur certains chemins de fallback. Elles doivent être vérifiées.

**Livrable P0 :** un rapport chiffré permettant de savoir où le JIT passe son temps.

---

## 3. Priorité P1 — Comprendre les blocs de 2 à 5 instructions

**Hypothèse :** la granularité des blocs limite les possibilités d'optimisation et augmente le poids des transitions.

Attention : des blocs courts ne sont pas intrinsèquement mauvais si le moteur sait les chaîner efficacement.

### 3.1 Examiner la construction des blocs

Dans `compemu_legacy_arm64_compat.cpp`, la fonction `execute_normal()` construit l'historique d'instructions avant `compile_block()`.

Conditions à examiner :

- `end_block(opcode)`.
- `regs.spcflags`.
- `MAXRUN`.
- `B2_JIT_MAXRUN`.
- Branchements conditionnels.
- Branchements arrière.
- Branches indirectes.
- Exceptions et traps.
- Interactions avec les interruptions et le timer.

`MAXRUN` est fixé à 1024 dans `compemu.h`, mais ce n'est pas nécessairement la longueur atteinte lors de la capture.

### 3.2 Classer les fins de blocs

Ajouter une classification précise :

- Branche conditionnelle.
- Branche inconditionnelle.
- `JSR` / `RTS`.
- `DBcc` / `DBRA`.
- Trap ou exception.
- `spcflags` non nul.
- Limite d'instructions atteinte.
- Fallback vers l'interpréteur.
- Sortie imposée par une règle de compatibilité.
- Autre.

Associer chaque événement à son PC 68k.

### 3.3 Étudier le traitement des boucles

Comparer avec Emu68 :

- Traduction au-delà de certains branchements.
- Incorporation des sauts locaux.
- Déroulement contrôlé de boucles.
- Gestion des branches arrière.
- Traces comportant plusieurs blocs élémentaires.
- Analyse des chemins fréquemment empruntés.

Paramètres Emu68 intéressants :

- `JCC_INLINE_RANGE`.
- `JCC_LOOP_COUNT`.
- `JCC_INSN_DEPTH`.

### 3.4 Expériences

Construire des microbenchmarks 68k :

1. Boucle `MOVEQ` / `ADDQ` / `DBRA`.
2. Boucle avec lecture mémoire.
3. Boucle avec écriture mémoire.
4. Boucle comportant un branchement conditionnel.
5. Appels répétés `JSR` / `RTS`.
6. Boucles avec code réparti sur plusieurs blocs.

Mesurer les instructions 68k/s et les cycles ARM par itération.

**Objectif :** déterminer si le moteur sait maintenir des boucles courtes dans un chemin natif continu.

---

## 4. Priorité P1 — Chaînage des blocs et coût du dispatcher

**Hypothèse :** le moteur gagne du temps en traduisant les instructions, puis perd ce gain dans les transitions.

### 4.1 Comparer les chemins

Étudier :

- Branchement direct entre blocs compilés.
- Branchement via un handler validé.
- Lookup dans une table de cache.
- Retour au dispatcher C.
- Retour à l'interpréteur.
- Retour pour interruption ou exception.

La distinction entre ces chemins est fondamentale.

### 4.2 Configuration macemu-jit

Fonctions et paramètres à examiner :

- `get_handler_for_edge()`.
- `jit_prefer_validated_successor_handler()`.
- `B2_JIT_PREFER_DIRECT_SUCCESSOR_HANDLER`.
- `B2_JIT_ENABLE_STABLE_DIRECT_EDGES`.
- `jit_source_edge_prefers_direct()`.
- `compemu_raw_endblock_pc_isconst()`.
- `compemu_raw_endblock_pc_inreg()`.

Le fork privilégie par défaut certaines entrées validées plutôt que les handlers directs.

Ce comportement protège la cohérence de l'état émulé, mais peut être coûteux.

### 4.3 Tests

- Comparer chaînage direct et validé.
- Mesurer les retours au dispatcher par million d'instructions 68k.
- Mesurer les transitions pour les blocs de 2–5 instructions.
- Vérifier si les branches conditionnelles sont efficacement chaînées.
- Comparer les branches directes et indirectes.
- Tester à cache entièrement chaud.

**Précaution :** ne pas désactiver les vérifications globalement sur la version de production avant validation de la cohérence des registres, des flags et des exceptions.

---

## 5. Priorité P1 — Checksums, invalidations et recompilations

**Hypothèse :** des vérifications ou invalidations répétées provoquent les ralentissements ponctuels.

### 5.1 Fonctionnement dans Basilisk II

Étudier :

- `flush_icache_lazy()`.
- `flush_icache_hard()`.
- `block_check_checksum()`.
- `check_checksum()`.
- `calc_checksum()`.
- `invalidate_block()`.
- `BI_ACTIVE`.
- `BI_NEED_CHECK`.
- `BI_NEED_RECOMP`.

Le JIT emploie déjà un mécanisme d'invalidation différée.

Une vérification de checksum n'est donc pas nécessairement effectuée à chaque exécution d'un bloc actif.

### 5.2 Questions à résoudre

- Combien d'invalidations surviennent par seconde ?
- Quelles opérations les déclenchent ?
- Combien de blocs sont concernés ?
- Quel pourcentage des checksums confirme que le code est inchangé ?
- Quel pourcentage provoque une recompilation ?
- Certaines invalidations concernent-elles des plages de mémoire beaucoup trop larges ?
- Les ralentissements coïncident-ils avec un pic de vérification ou de compilation ?
- Le code modifié dynamiquement par System 7 est-il correctement détecté ?

### 5.3 Comparer avec Emu68

Emu68 utilise :

- Un identifiant de génération du cache (*epoch*).
- Un mécanisme de réutilisation des traductions anciennes.
- Une vérification différée des unités de traduction.
- Un système appelé **JIT Dumpster**.
- Une vérification CRC32 dans certains chemins d'exécution.

Sources :

- [Emu68 — ExecutionLoop.c](https://github.com/michalsc/Emu68/blob/master/src/ExecutionLoop.c)
- [Emu68 — M68k_Translator.c](https://github.com/michalsc/Emu68/blob/master/src/M68k_Translator.c)

Le principe intéressant est de conserver et de réactiver du code traduit plutôt que de le détruire systématiquement.

### 5.4 Étudier les invalidations ciblées

Apple avait documenté dès 1995 :

- `FlushInstructionCache`.
- `FlushCodeCacheRange`.

La seconde permettait une invalidation limitée à une plage d'adresses.

Pour Okapia, étudier :

- Le suivi des pages contenant du code exécuté.
- Les invalidations par plage d'adresses.
- Les générations de pages ou de blocs.
- Les checksums à la demande.
- La réactivation de traductions précédentes.
- La détection des écritures sur du code compilé.

Un cache plus intelligent peut être plus utile qu'un cache simplement plus grand.

### 5.5 Contraintes

Ne pas désactiver aveuglément les checksums.

Mac OS classique peut exécuter du code chargé dynamiquement, des segments déplacés ou des portions de code modifiées en mémoire.

Toute optimisation doit préserver les mécanismes de cohérence.

**Objectif :** éliminer les invalidations inutiles et réduire les recompilations, sans compromettre System 7.

---

## 6. Priorité P1/P2 — Qualité du code ARM64 généré

**Hypothèse :** le code compilé contient beaucoup trop d'instructions auxiliaires.

### 6.1 Mesurer l'expansion du code

Pour plusieurs blocs représentatifs :

- Compter les instructions 68k traduites.
- Compter les instructions ARM64 générées.
- Identifier les instructions ARM64 utiles.
- Identifier les instructions de gestion du JIT.
- Mesurer le coût du prologue et de l'épilogue.
- Mesurer les chargements et écritures de registres.
- Compter les appels vers des helpers C.

Analyser du code désassemblé, pas seulement le code source du générateur.

### 6.2 Fin de bloc ARM64

Dans `codegen_arm64.cpp`, examiner :

- `compemu_raw_endblock_pc_isconst()`.
- `compemu_raw_endblock_pc_inreg()`.
- Mise à jour du compteur de cycles.
- Vérification de `spcflags`.
- Sauvegarde de `regs.pc_p`.
- Sauvegarde de `regs.pc`.
- Sauvegarde de `regs.pc_oldp`.
- Gestion des branchements.
- Gestion des flags.

Le fork contient des synchronisations supplémentaires introduites pour corriger des problèmes de cohérence entre blocs compilés et instructions interprétées.

### 6.3 Écritures redondantes

Le chemin `compemu_raw_endblock_pc_isconst()` présente notamment des écritures successives de `regs.pc_p`.

Vérifier si certaines peuvent être fusionnées ou éliminées, sans provoquer de régression.

### 6.4 Barrières mémoire ARM64

Le code contient des barrières `DMB ISH` sur certains chemins de fin de bloc.

Étudier :

- Leur coût sur Cortex-A72.
- Leur nécessité exacte.
- Leur interaction avec les interruptions Circle.
- La cohérence mémoire entre cœurs.
- Les possibilités de placement moins fréquent.

Le fait que l'émulation soit monocœur ne garantit pas que toutes les barrières soient inutiles.

### 6.5 Comparaison historique Apple

Le DR Emulator d'Apple introduit en 1995 annonçait une moyenne inférieure à quatre instructions PowerPC par instruction 68k compilée.

Cette valeur ne peut pas être directement transposée à ARM64, mais sert de référence historique pour la compacité du code généré.

Comparer notamment :

- La traduction des opérations simples.
- La représentation des flags.
- Le coût des accès mémoire.
- Le traitement des branches.
- La synchronisation de l'état CPU.

**Objectif :** identifier les instructions ARM64 éliminables sur le chemin d'exécution fréquent.

---

## 7. Priorité P2 — Gestion des registres et du CCR

**Hypothèse :** trop de sauvegardes et de restaurations limitent les gains du JIT.

### 7.1 Registres 68k

Étudier :

- Conservation des registres D0–D7 et A0–A7 dans les registres ARM64.
- Nombre de chargements depuis la structure `regs`.
- Nombre d'écritures vers `regs`.
- Allocation et libération des registres temporaires.
- Effet des appels de fonctions C.
- Gestion des valeurs constantes.
- Possibilité d'optimisations entre blocs.

### 7.2 Flags CCR

Le 68k utilise les flags X, N, Z, V et C.

ARM64 fournit NZCV mais avec des différences de sémantique, notamment concernant le carry et le flag X.

Examiner :

- `regflags.nzcv`.
- `regflags.x`.
- `needed_flags`.
- `liveflags`.
- `flush_flags()`.
- `dont_care_flags()`.
- `MSR NZCV`.
- `MRS NZCV`.
- `flags_carry_inverted`.

### 7.3 Optimisations

- Ne générer que les flags nécessaires aux instructions suivantes.
- Éviter les conversions répétées des flags.
- Maintenir l'état natif entre blocs chaînés quand cela est sûr.
- Éviter les restaurations de registres inutiles.
- Comparer l'analyse de vivacité des flags avec Emu68.

**Objectif :** réduire les accès mémoire et le nombre d'instructions ARM64 par opération 68k.

---

## 8. Priorité P2 — Couverture native des instructions 68k

**Hypothèse :** le JIT ne compile qu'une partie des instructions réellement utilisées.

### 8.1 Niveaux d'optimisation

Examiner :

- `optlev=0` : chemin sans véritable traduction native du bloc.
- `optlev=1` : niveau intermédiaire.
- `optlev=2` : génération native complète lorsque disponible.
- `B2_JIT_MAX_OPTLEV`.
- `cpu_compatible`.
- `B2_JIT_FORCE_OPTLEV0`.

Le fork utilise des règles spécifiques pour le code situé en RAM et en ROM.

Certaines plages de ROM sont volontairement limitées à `optlev=0`.

### 8.2 Mesurer la couverture dynamique

Compter, par famille d'opcodes :

- Nombre d'exécutions natives.
- Nombre de fallbacks.
- Nombre de cycles ou temps passés.
- Nombre d'instructions ARM64 générées.

Familles à examiner en priorité :

- `MOVE` / `MOVEQ`.
- `ADD` / `SUB`.
- `CMP` / `TST`.
- `Bcc` / `DBcc`.
- `JSR` / `RTS`.
- `MOVEM`.
- Décalages et rotations.
- Opérations mémoire.
- Instructions privilégiées.
- Instructions FPU.

### 8.3 Fallbacks

Le fallback peut entraîner :

1. Sauvegarde de l'état ARM64.
2. Synchronisation du PC.
3. Appel de l'interpréteur C.
4. Restauration des registres et des flags.
5. Reprise de la traduction native.

Sur un bloc de trois instructions, un seul fallback peut absorber une grande partie du bénéfice.

### 8.4 FPU

Le backend ARM64 étudié ne dispose pas nécessairement d'un FPU JIT complet.

Il faut distinguer les tests entiers, graphiques et flottants de Speedometer.

Le FPU n'est probablement pas prioritaire pour les transferts SMB, qui reposent principalement sur des opérations entières, mémoire et réseau.

**Objectif :** améliorer d'abord la couverture des instructions les plus exécutées, pas nécessairement celle du plus grand nombre d'opcodes différents.

---

## 9. Priorité P2 — Timer, cycles et interruptions Circle

Le JIT doit respecter les interruptions Macintosh et les contraintes temporelles de System 7.

### 9.1 Chemins à auditer

- `cpu_check_ticks()`.
- `cpu_do_check_ticks()`.
- `SPCFLAG_INT`.
- `SPCFLAG_JIT_EXEC_RETURN`.
- `countdown`.
- `compemu_raw_maybe_do_nothing()`.
- Vérifications de `spcflags` en milieu et en fin de bloc.

### 9.2 Questions

- Quelle est la fréquence réelle des contrôles ?
- Un flag reste-t-il actif plus longtemps que prévu ?
- Les interruptions provoquent-elles des sorties trop fréquentes ?
- Existe-t-il une dépendance inutile à une synchronisation ARM coûteuse ?
- Le contrôle des ticks est-il correctement adapté à Circle ?
- La fréquence des contrôles varie-t-elle selon la longueur des blocs ?

### 9.3 Optimisations possibles

- Réduire les contrôles redondants.
- Garder les chemins sans interruption aussi courts que possible.
- Sortir du JIT uniquement lorsque nécessaire.
- Maintenir une latence d'interruption correcte.
- Mesurer la fréquence des interruptions pendant Speedometer.

**Contrainte :** conserver le fonctionnement du Device Manager, des E/S asynchrones et du timer Macintosh.

---

## 10. Priorité P2 — Mémoire, MMU et caches ARM

Même si l'exécution du code généré fonctionne déjà, vérifier son coût.

### 10.1 Cache de traduction

Examiner :

- Taille réellement utilisée.
- Taux de remplissage.
- Politique d'éviction.
- Fragmentation.
- Nombre de vidages globaux.
- Proximité des blocs fréquemment exécutés.
- Comportement après saturation.

### 10.2 Caches matériels

Le Raspberry Pi 4 dispose de caches d'instructions et de données distincts.

Vérifier :

- Synchronisation I-cache/D-cache après génération ou modification du code.
- Fréquence des opérations de maintenance de cache.
- Permissions mémoire du code JIT.
- Propriétés mémoire utilisées par Circle.
- Alignement des blocs ARM64.
- Effet de la taille du code généré sur le cache L1 d'instructions.

### 10.3 Adressage mémoire 68k

Comparer :

- Traduction des adresses invitées.
- Accès directs à la RAM émulée.
- Accès à la ROM.
- Accès mémoire spéciaux.
- Conversion big-endian / little-endian.
- Protection et contrôles de validité.
- Appels aux fonctions mémoire C.

L'environnement Emu68 utilise une organisation mémoire différente de Basilisk II et ne constitue pas un remplacement direct.

**Objectif :** supprimer les coûts inutiles sur les accès mémoire courants, sans casser l'émulation des adresses particulières.

---

## 11. Priorité P2/P3 — Étudier Emu68

**Référence principale :** [michalsc/Emu68](https://github.com/michalsc/Emu68)

Emu68 est particulièrement pertinent parce qu'il s'agit d'un traducteur dynamique 68k → ARM64 conçu pour fonctionner sur du matériel Raspberry Pi.

### 11.1 Mécanismes à comparer

| Mécanisme | Question |
|---|---|
| Construction des traces | Pourquoi Emu68 peut-il traduire au-delà de certaines branches ? |
| Boucles | Comment sont détectées et déroulées les boucles courtes ? |
| Cache LRU | Comment accélérer la recherche des blocs déjà traduits ? |
| Hash table | Quel coût pour retrouver une unité de traduction ? |
| JIT Dumpster | Comment réactiver les anciens blocs après invalidation ? |
| CRC32 / epoch | Quand une traduction est-elle réellement revérifiée ? |
| Registres natifs | Comment limiter les sauvegardes d'état ? |
| Flags | Comment éviter les calculs inutiles ? |
| Prologues et épilogues | Quel coût fixe par unité de traduction ? |
| Exits conditionnels | Comment sont gérées les branches internes ? |

### 11.2 Fichiers prioritaires

- [`src/M68k_Translator.c`](https://github.com/michalsc/Emu68/blob/master/src/M68k_Translator.c) — génération des unités et traitement des branches.
- [`src/ExecutionLoop.c`](https://github.com/michalsc/Emu68/blob/master/src/ExecutionLoop.c) — dispatch, recherche des blocs, LRU et validation.
- [`src/LRUCache.cpp`](https://github.com/michalsc/Emu68/blob/master/src/LRUCache.cpp) — autre implémentation de cache présente dans le projet.
- [`include/RegisterAllocator.h`](https://github.com/michalsc/Emu68/blob/master/include/RegisterAllocator.h) — allocation des registres.
- [`src/cache.c`](https://github.com/michalsc/Emu68/blob/master/src/cache.c) — gestion des caches mémoire émulés.

### 11.3 Ce qu'il ne faut pas faire immédiatement

Éviter de remplacer tout le backend Basilisk II par Emu68 avant de comprendre les différences architecturales.

Un tel port impliquerait une adaptation significative de :

- L'état CPU 68k.
- La gestion mémoire.
- L'adressage.
- Les exceptions.
- Les traps Macintosh.
- Les interruptions.
- Les interfaces avec Basilisk II.

Commencer par réutiliser les principes et algorithmes pertinents.

---

## 12. Priorité P3 — Comparer les autres JIT Amiga

### 12.1 UAE4ARM et Amiberry

Références :

- [Amiberry](https://github.com/BlitterStudio/amiberry)
- [Amiberry-Lite](https://github.com/BlitterStudio/amiberry-lite)

Ces moteurs reposent sur la famille UAE/WinUAE et possèdent des liens historiques avec les générateurs utilisés dans certains forks de Basilisk II.

Comparer :

- La gestion des branches.
- Les optimisations natives.
- Les tables d'instructions compilables.
- Les fallbacks.
- Les stratégies d'allocation de registres.
- La gestion de la mémoire Amiga.
- Les différences ARM32 / ARM64.

### 12.2 Benchmarks existants

Des résultats SysInfo publiés donnent notamment :

| Moteur | Score rapporté |
|---|---:|
| Amiberry 4.2 JIT / Pi 4 | 287 MIPS |
| UAE4ARM JIT / Pi 4 | 488 MIPS |
| Emu68 / présentation FOSDEM 2024 | 824 MIPS |
| Emu68 / Pi 4, estimations rapportées | 1 600–2 000 MIPS |

Les versions, configurations et matériels ne sont pas suffisamment homogènes pour permettre une comparaison rigoureuse.

Ces scores SysInfo ne doivent pas être assimilés à des millions d'instructions 68k effectivement exécutées par seconde.

Référence : [PiStorm — Benchmarks](https://pistorm.github.io/docs/benchmarks/).

### 12.3 Benchmark comparatif idéal

Exécuter un même microprogramme 68k :

- Sur Okapia sans JIT.
- Sur Okapia avec JIT.
- Sur Emu68.
- Éventuellement sur Amiberry.

Comparer le temps d'exécution réel et, si possible, les cycles matériels et les instructions ARM64 exécutées.

Le benchmark doit être indépendant des périphériques et des services du système invité.

---

## 13. Priorité P3 — Étudier le JIT historique Apple

### 13.1 Première génération : 1994

Les premiers Power Macintosh utilisent un émulateur 68k essentiellement fondé sur l'interprétation.

### 13.2 Deuxième génération : 1995

Apple introduit un émulateur à recompilation dynamique 68k → PowerPC avec les Power Macintosh de seconde génération.

Techniques intéressantes :

- Traduction à la demande.
- Conservation du code traduit.
- Réutilisation des instructions compilées.
- Optimisation des séquences fréquemment exécutées.
- Gestion des transitions entre code 68k et code natif.
- Invalidation de plages de code.
- Coût de génération relativement faible.

### 13.3 Documentation historique

[Power Macintosh 9500 — Developer Note, Apple, 1995](https://leopard-adc.pepas.com/documentation/Hardware/Developer_Notes/Macintosh_CPUs-PPC_Desktop/PPC_9500.pdf)

Apple y présente notamment une moyenne inférieure à quatre instructions PowerPC par instruction 68k traduite.

Autre référence :

*develop — The Apple Technical Journal*, numéro 23, septembre 1995, article de Dave Evans sur la recompilation dynamique.

### 13.4 Intérêt et limites

L'approche Apple est intéressante car elle ciblait précisément Mac OS classique, avec ses traps, ses segments de code et ses contraintes de compatibilité.

Cependant :

- Le processeur cible est PowerPC, non ARM64.
- L'architecture des caches et de la mémoire diffère.
- Le code source du DR Emulator n'est pas publiquement disponible à notre connaissance.
- Une rétro-ingénierie du binaire serait plus coûteuse que l'étude de moteurs modernes ouverts.

**Priorité :** étude conceptuelle avant une éventuelle analyse binaire.

---

## 14. Protocole de benchmark recommandé

### 14.1 Niveaux de mesure

| Test | Ce qu'il mesure |
|---|---|
| Boucle entière en registres | Qualité du code natif |
| Boucle avec mémoire | Coût des accès mémoire |
| Boucle avec branchements | Coût du chaînage |
| Appels de fonctions répétés | Coût des transitions |
| Instructions non compilées | Coût du fallback |
| Boucle à cache chaud | Exécution native sans compilation |
| Code fréquemment modifié | Gestion des invalidations |
| Speedometer CPU | Performance CPU applicative |
| Speedometer Graphics | CPU + QuickDraw |
| Speedometer Math | Calculs entiers et flottants selon le sous-test |
| Démarrage System 7 | Performances et compatibilité globale |
| Transfert Mamba SMB | Performance réelle sur un cas d'usage Okapia |

### 14.2 Conditions de test

- Mesures interpréteur/JIT sur le même firmware.
- Même fréquence CPU.
- Même configuration Macintosh.
- Même RAM.
- Même version système.
- Cache froid puis cache chaud.
- Plusieurs répétitions.
- Analyse des valeurs médianes et des ralentissements extrêmes.
- Aucun diagnostic série actif pendant les mesures de performance.

### 14.3 Mesures particulièrement utiles

- Ratio de performance JIT/interpréteur.
- Instructions 68k/s.
- Temps CPU de compilation.
- Temps CPU de validation.
- Fréquence des invalidations.
- Instructions ARM64 par instruction 68k.
- Cycles ARM par instruction 68k, quand mesurables.
- Pourcentage d'instructions réellement exécutées nativement.
- Longueur moyenne des séquences natives sans retour au C.

---

## 15. Matrice de diagnostic

| Résultat observé | Piste prioritaire |
|---|---|
| JIT lent sur boucle entièrement native | Génération ARM64 et gestion des registres |
| JIT rapide en boucle simple mais lent dans Speedometer | Fallbacks, mémoire ou services système |
| JIT lent uniquement avec beaucoup de branches | Chaînage, dispatcher, prologues/épilogues |
| JIT rapide à cache chaud mais lent à froid | Compilation et stratégie de cache |
| Ralentissements synchronisés avec des invalidations | Checksums, recompilations et flush |
| Beaucoup de blocs `optlev=0` | Couverture native insuffisante |
| Beaucoup de fallbacks C dans les blocs L2 | Implémentation des opcodes manquants |
| Beaucoup de blocs courts mais très peu de retours C | Optimiser le coût des transitions natives |
| Beaucoup de blocs courts avec retours C fréquents | Revoir le dispatch et le chaînage |
| Gros volume de code ARM64 par opcode | Allocation de registres et synchronisations |
| Faibles performances dans les tests FPU uniquement | Backend FPU |
| Bons microbenchmarks mais faibles gains SMB | Autres goulots : mémoire, TCP/IP, services Mac OS |

---

## 16. Ordre de travail recommandé

### Phase 1 — Comprendre le problème

- [ ] Figer une version de référence du JIT.
- [ ] Mesurer Speedometer avec et sans JIT.
- [ ] Désactiver les diagnostics coûteux.
- [ ] Ajouter des compteurs en mémoire.
- [ ] Obtenir un histogramme pondéré des longueurs de blocs.
- [ ] Classer les raisons de fin de bloc.
- [ ] Mesurer la couverture réelle L2 native.
- [ ] Mesurer les retours au dispatcher C.

**Critère de sortie :** identifier le principal coût responsable de l'absence d'accélération.

### Phase 2 — Identifier les ralentissements ponctuels

- [ ] Instrumenter les checksums.
- [ ] Compter les invalidations.
- [ ] Compter les recompilations.
- [ ] Distinguer flush global et invalidation différée.
- [ ] Corréler les événements avec les ralentissements.
- [ ] Comparer le mécanisme avec Emu68 JIT Dumpster.

**Critère de sortie :** déterminer si les pauses proviennent du cache de traduction.

### Phase 3 — Optimiser les blocs courts

- [ ] Mesurer le coût des handlers validés.
- [ ] Tester le chaînage direct.
- [ ] Examiner les vérifications `spcflags`.
- [ ] Examiner les barrières mémoire.
- [ ] Réduire les synchronisations de PC redondantes.
- [ ] Comparer les blocs et boucles avec Emu68.
- [ ] Expérimenter des traces plus longues ou un chaînage plus efficace.

**Critère de sortie :** accélérer les boucles de 2–5 instructions sans perdre en compatibilité.

### Phase 4 — Optimiser le code généré

- [ ] Désassembler des blocs ARM64 typiques.
- [ ] Compter les instructions utiles et auxiliaires.
- [ ] Étudier l'allocation des registres.
- [ ] Optimiser les flags CCR.
- [ ] Identifier les fallbacks les plus fréquents.
- [ ] Implémenter prioritairement les familles d'opcodes coûteuses.
- [ ] Réduire les appels de helpers C.

**Critère de sortie :** améliorer les instructions 68k/s sur les microbenchmarks.

### Phase 5 — Valider l'ensemble

- [ ] Démarrage complet de System 7.
- [ ] Tests QuickDraw et Speedometer.
- [ ] Fonctionnement des interruptions.
- [ ] Tests disque et réseau.
- [ ] Transferts SMB Mamba.
- [ ] Gestion des exceptions 68k.
- [ ] Tests de code modifié dynamiquement.
- [ ] Exécutions prolongées sans corruption mémoire.
- [ ] Comparaison finale JIT/interpréteur.

---

## 17. Références principales

### Implémentation actuelle

**macemu-jit**

https://github.com/khawkins98/macemu-jit

Fichiers prioritaires :

- `BasiliskII/src/uae_cpu_2026/compiler/compemu_support_arm.cpp`
- `BasiliskII/src/uae_cpu_2026/compiler/codegen_arm64.cpp`
- `BasiliskII/src/uae_cpu_2026/compiler/compemu_midfunc_arm64.cpp`
- `BasiliskII/src/uae_cpu_2026/compiler/compemu_legacy_arm64_compat.cpp`
- `BasiliskII/src/uae_cpu_2026/compiler/compemu.h`

### Références comparatives

**Emu68**  
https://github.com/michalsc/Emu68

**Amiberry**  
https://github.com/BlitterStudio/amiberry

**Amiberry-Lite**  
https://github.com/BlitterStudio/amiberry-lite

**PiStorm — Benchmarks**  
https://pistorm.github.io/docs/benchmarks/

**Apple Power Macintosh 9500 Developer Note**  
https://leopard-adc.pepas.com/documentation/Hardware/Developer_Notes/Macintosh_CPUs-PPC_Desktop/PPC_9500.pdf

---

## 18. Conclusion et recommandation

Le JIT ARM64 d'Okapia a franchi une étape importante : la génération et l'exécution de code natif fonctionnent, et les blocs sont effectivement réutilisés.

Mais cette réussite fonctionnelle ne démontre pas encore que la traduction apporte un avantage en temps CPU.

Les hypothèses prioritaires sont :

**1. Rendement des petits blocs**  
Les blocs de 2 à 5 instructions peuvent être trop coûteux à exécuter si chaque transition nécessite de nombreuses opérations auxiliaires ou des retours au C.

**2. Chaînage insuffisamment efficace**  
Les chemins validés, les synchronisations d'état et le dispatch peuvent annuler les gains du code ARM64.

**3. Couverture native incomplète**  
Les instructions exécutées par fallback peuvent provoquer des sauvegardes/restaurations fréquentes.

**4. Invalidations et checksums**  
Les mécanismes de validation peuvent expliquer les ralentissements ponctuels, particulièrement si le même code est régulièrement invalidé puis réactivé ou recompilé.

**5. Qualité du code ARM64 généré**  
Le coût des accès mémoire, des flags et des fonctions auxiliaires peut rester trop important.

### Décision proposée

**Ne pas remplacer immédiatement le JIT de Basilisk II.**

Commencer par un profilage rigoureux, puis confronter les résultats aux choix techniques d'Emu68.

Emu68 constitue la référence technique principale, parce que son code ARM64 et ses mécanismes de traduction, de cache et de gestion des boucles sont accessibles.

Le DR Emulator d'Apple constitue une référence historique utile pour les principes de recompilation 68k, la compatibilité avec Mac OS et les invalidations ciblées.

La priorité absolue est de déterminer **combien d'instructions 68k sont exécutées nativement entre deux sorties coûteuses**, et combien coûte chaque transition.

C'est cette mesure qui permettra de choisir entre améliorer le moteur existant, lui emprunter des techniques d'Emu68, ou envisager ultérieurement une architecture de JIT différente.