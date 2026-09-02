# Okapia

## Macintosh classique bare-metal sur Raspberry Pi — Plan de projet

> Document unique du projet : décisions, architecture, phases, et l'annexe des sources en fin de fichier.
> Toute affirmation technique y porte sa référence `fichier:ligne` ou sa source, vérifiée le 2026-08-24.

---

## 1. Objectif

Faire tourner **Mac OS 68k sans système d'exploitation hôte** sur Raspberry Pi, en portant **Basilisk II**
sur **Circle**.

La machine cible est un **Quadra 950 virtuel** :

| | |
|---|---|
| CPU | 68040 émulé (interpréteur) |
| RAM | **256 Mo**, la limite native du Quadra 900/950 |
| ROM | **Quadra 650** (`Q650.ROM`), 32-bit clean |
| Système | System 7.x → Mac OS 8.1 |
| Vidéo | carte NuBus virtuelle, résolutions et profondeurs multiples, changeables à chaud |
| Entrées | clavier et souris USB présentés comme périphériques ADB |
| Stockage | image disque sur carte SD, **plus un dossier partagé avec l'hôte** |
| Réseau | Ethernet, le Mac étant un nœud à part entière du réseau local |
| Audio | HDMI |
| Démarrage | mise sous tension → carillon → Happy Mac → Mac OS, sans rien d'autre à l'écran |
| Configuration | **firmware Okapia** au look Apple plausible, appelable au démarrage — pas seulement un fichier texte |
| Performance | nettement supérieure à un Quadra réel, **sans JIT** |

Cible matérielle : **Raspberry Pi 4** pour l'usage, **Pi 3** pour le développement (c'est le modèle que QEMU
émule), **Pi 5** comme cible ultérieure avec un chemin vidéo dégradé. Le développement initial se fait
entièrement sous **QEMU**, sans carte.

### Portée à plus long terme

Basilisk II est le **premier moteur**, pas le seul horizon. La couche bare-metal est conçue pour accueillir
d'autres moteurs d'émulation — **SheepShaver** (PowerPC, Mac OS 8.5 → 9.0.4) en premier lieu, et pourquoi
pas Mini vMac vers le bas — afin d'élargir la couverture des systèmes et des logiciels, dans l'esprit de ce
que propose *Infinite Mac*. Voir §3.5 et §18.

### Ce que le projet n'est pas

- Ce n'est pas une émulation fidèle au cycle près d'une machine Apple particulière.
- Ce n'est pas un projet de JIT. L'interpréteur suffit à dépasser un Quadra ; le JIT est une piste
  ultérieure, hors du chemin critique.
- Ce n'est pas « sans binaire propriétaire » : Circle dépend du firmware Raspberry Pi (`config.txt`,
  fichiers `.dtb`). L'objectif est l'absence de **système d'exploitation hôte**, pas l'absence de blob.

---

## 2. Décisions actées

| Sujet | Décision |
|---|---|
| **Principe directeur** | Écrire le moins de code neuf possible. Tout apport amont qui évite du code est préféré. |
| RAM Mac | **256 Mo**. Basilisk plafonne à 1023 Mo ; lever notre limite est laissé à la communauté. |
| Adressage | **`DIRECT_ADDRESSING`** |
| Cœur CPU | **`uae_cpu_2021`** — c'est celui que macemu compile sur ARM/AArch64 |
| Cœur FPU | **`fpu_uae`** (aucune dépendance externe) |
| Bibliothèque C/C++ | **circle-stdlib, `STDLIB_SUPPORT=3`** (newlib + libstdc++) |
| Vidéo | **framebuffer de sortie fixe + compositeur logiciel**, sur tous les modèles |
| Multicœur | **S1** : émulation 68k sur un cœur secondaire, cœur 0 aux IRQ et aux I/O |
| Stockage | image disque sur SD **+ dossier partagé** (`extfs`) dès la première version utile |
| Horloge | **NTP → RTC (Pi 5 seulement) → dernière valeur sur SD** |
| PRAM | persistance **déclenchée par l'écriture réelle**, pas de sondage périodique |
| Arrêt | arrêt propre requis, plus vidage périodique du cache disque |
| Réseau | **partage de l'adresse MAC du Pi** ; slirp en second recours |
| Modularité | `src/circle/` conçu comme `src/SDL/` amont, pour accueillir **SheepShaver** ensuite | 
| JIT | hors périmètre initial |
| Licence | **GPLv3** |
| Nom du projet | **Okapia** — nom de la plateforme ; les moteurs gardent le leur (Basilisk II, SheepShaver) |

---

## 3. Principes architecturaux

### 3.1 Écrire le moins de code possible

C'est le principe qui arbitre tous les autres. Avant d'écrire un fichier, vérifier s'il existe en amont —
dans `macemu`, dans Circle, dans circle-stdlib — et l'adapter plutôt que le réinventer. Voir §8.

### 3.2 Rester proche des upstreams

Le projet doit compiler comme :

```
Basilisk II amont  +  notre couche platform/circle  +  Circle (via circle-stdlib)
```

Les modifications du cœur Basilisk doivent être : isolées, documentées, stockées dans `patches/macemu/`,
et proposées en amont quand elles ont un intérêt général. À ce jour deux patches sont prévus, tous deux
minuscules et tous deux de bons candidats à une remontée : le drapeau d'écriture PRAM (§7.7) et la prise en
compte de l'écriture d'horloge (§7.6).

### 3.3 M5Tab-Macintosh et BMC64 sont des références, pas des dépendances

- **M5Tab-Macintosh** (BasiliskII sur ESP32-P4) : preuve qu'un portage embarqué de Basilisk fonctionne, et
  source d'idées sur le suivi des zones modifiées et la gestion des écritures SD. **Aucune licence n'est
  déclarée sur ce dépôt** : ne pas en copier de code sans clarification écrite avec son auteur.
- **BMC64** (VICE bare-metal sur Circle, GPL-3.0) : le modèle architectural le plus proche — boucle
  d'émulation, cadencement, audio, framebuffer, USB et configuration sur SD, le tout dans Circle.

Les cloner dans `reference/`, ignoré par Git.

### 3.5 Modularité : préparer d'autres moteurs sans les attendre

**Fait structurant, vérifié :** dans le dépôt macemu, **SheepShaver réutilise BasiliskII par 129 liens
symboliques** — `adb.cpp`, `audio.cpp`, `timer.cpp`, `xpram.cpp`, `prefs.cpp`, `disk.cpp`, `cdrom.cpp`,
`extfs.cpp`, `slirp/`, `CrossPlatform/`, les `dummy/`… **et le répertoire `SDL/` en entier**, partagé via
`#ifdef SHEEPSHAVER` (22 occurrences dans `video_sdl2.cpp`).

Autrement dit : **la couche plateforme de macemu est déjà conçue pour servir les deux émulateurs.** Si notre
`src/circle/` occupe la même place que `src/SDL/`, SheepShaver le réutilisera par construction, sans travail
d'architecture supplémentaire. C'est un acquis, pas un pari.

**Trois règles de conception à respecter dès maintenant**, toutes peu coûteuses :

1. **`src/circle/` est l'équivalent de `src/SDL/`** — mêmes fichiers, mêmes rôles, mêmes points d'entrée.
   Là où l'amont teste `#ifdef SHEEPSHAVER`, garder le test plutôt que l'élaguer, même sans l'implémenter.
2. **Séparer le matériel de l'émulateur.** Une couche `hal_circle.{h,cpp}` — framebuffer, entrées, son,
   trames réseau, fichiers, horloge — au-dessus de laquelle les `*_circle.cpp` ne sont que des adaptateurs
   vers l'API de macemu. C'est ce qui permettrait un jour d'accueillir un moteur **hors macemu**
   (Mini vMac, DingusPPC) sans tout réécrire.
3. **Le choix du moteur et du profil est une donnée de configuration**, lue au démarrage depuis la SD, pas
   une constante de compilation. Le gestionnaire d'amorçage viendra plus tard, mais la structure doit
   l'admettre dès le départ.

**Ce qui ne doit pas être fait maintenant** : implémenter quoi que ce soit pour SheepShaver. Aucune
abstraction spéculative, aucune indirection « au cas où ». La modularité recherchée ici est celle que
l'amont fournit déjà ; il suffit de ne pas la casser.

**Deux obstacles réels côté SheepShaver**, à connaître avant de promettre quoi que ce soit (§18) :

- **il exige des adresses hôtes fixes** — `RAM_BASE = 0x10000000`, `ROM_BASE = 0x50000000`, réservées par
  `vm_acquire_fixed` (`SheepShaver/src/Unix/main_unix.cpp:183-185`). Sous Circle, où le mappage est
  l'identité, ces plages entrent en conflit avec le tas ; il faudra les réserver ou patcher l'amont. C'est
  plus contraignant que le `DIRECT_ADDRESSING` de Basilisk ;
- **son JIT PowerPC n'a pas de backend AArch64 en amont** : `kpx_cpu/src/cpu/jit/` ne contient que `x86`,
  `amd64`, `mips`, `ppc` et `dummy`. Sur Raspberry Pi, SheepShaver tournerait donc en **interprété**, ce qui
  est lent. Un backend AArch64 existe dans `rcarmo/macemu-jit`, mais son gain n'est pas démontré. La
  faisabilité d'un SheepShaver **utilisable** sur Pi est donc une question ouverte, à mesurer, pas à
  annoncer.

### 3.4 Architecture cible

```
                    Basilisk II (amont)
   uae_cpu_2021 · ROM patches · ADB · disk · SCSI · video · audio · ether · timer
                              │
              ┌───────────────┴───────────────┐
              │                               │
   couches Unix réutilisées          src/circle/ (à écrire)
   extfs · prefs · xpram             video · input · audio · ether · main
   timer · accès images disque
              │                               │
              └───────────────┬───────────────┘
                              ▼
                     circle-stdlib (newlib + libstdc++)
                              ▼
                           Circle
                              ▼
                       Raspberry Pi
```

---

## 4. Dépôts

### 4.1 Dépendances de build

| Rôle | Dépôt | Notes |
|---|---|---|
| Environnement bare-metal + libc | **`codeberg.org/larchcone/circle-stdlib`** | dépôt canonique ; GitHub n'est qu'un miroir en lecture seule. **Inclut Circle comme sous-module.** |
| Cœur d'émulation | **`github.com/kanjitalk755/macemu`** | fork actif de `cebix/macemu`. **Ne pas utiliser `emaculation/macemu`, dormant depuis avril 2022.** |

Épingler chacun sur un SHA précis. Jamais `master`/`main`/`latest`.

Conséquence de circle-stdlib : **Circle n'est plus épinglé directement** mais à travers circle-stdlib, qui
épingle sa propre version. Mettre à jour Circle seul n'est pas possible sans risque — c'est le prix du
choix, assumé.

### 4.2 Références, hors build

Matériel d'étude, **cloné à la demande et non par défaut** : chaque dépôt tiré est du disque occupé et une
invitation à s'éparpiller. `scripts/fetch-reference.sh` sans argument liste ce qui existe et ce qui est déjà
local ; avec un nom, il clone en superficiel dans `reference/`, ignoré par Git.

| Dépôt | Quand il devient utile |
|---|---|
| `bmc64` | dès l'écriture de `main_circle.cpp` — boucle d'émulation, cadencement, audio, framebuffer sur Circle |
| `M5Tab-Macintosh` | au portage de Basilisk et à la vidéo (tuiles sales). **Lecture seule : aucune licence déclarée** |
| `infinite-mac` | au provisionnement des images et au catalogue de systèmes |
| `macemu-jit` | pour son corpus de tests ; pour son JIT seulement si le sujet s'ouvre |
| `amiberry` | uniquement si le JIT s'ouvre |

Aucune n'est nécessaire avant la phase 3. Quand une nouvelle devient utile en cours de route, elle se
demande explicitement — elle ne se tire pas par réflexe.

### 4.3 Épinglage et mise à jour

Les deux amonts n'ont pas les mêmes usages, la politique s'y adapte.

| | Pratique amont | Ce qu'on épingle |
|---|---|---|
| **circle-stdlib** | versions taguées régulières (`v20` à ce jour, qui coïncide avec `master`) | **le tag** — lisible, comparable, associé à des notes de version. Circle vient avec, épinglé par circle-stdlib : nous n'avons pas à le gérer séparément |
| **macemu** | pas de version utile — les derniers tags et *releases* datent de 2017, le travail se fait sur `master` (~45 commits sur les douze derniers mois) | **un SHA**, sans alternative |

**Ne jamais lancer `git submodule update --remote`** : cette commande déplace les pins en aveugle. La
mise à jour est toujours un acte délibéré.

**Voir le retard** — `scripts/check-upstreams.sh` affiche, pour chaque sous-module, le SHA épinglé, sa date,
son tag éventuel, et les commits amont non intégrés. Il *fetch* mais ne modifie rien : c'est le seul script
qui a besoin du réseau. Le SHA épinglé vit dans l'index Git, pas dans un tableau de documentation qui
divergerait.

**Déplacer un pin** — jamais en même temps qu'un autre changement, et pas sans validation :

- [ ] lire les commits amont concernés (`check-upstreams.sh` en donne la liste)
- [ ] `cd` dans le sous-module, `git checkout <tag ou SHA>`
- [ ] réappliquer les patches de `patches/macemu/` — **si l'un ne s'applique plus, c'est l'information la
      plus utile de l'opération** : le régler avant d'aller plus loin
- [ ] compiler pour QEMU, dérouler les tests hôte
- [ ] démarrer Mac OS sous QEMU
- [ ] valider sur matériel
- [ ] comparer au relevé de référence (§ phase 11)
- [ ] commit dédié, dont le message dit **pourquoi** cette montée de version

**Cadence** : sur besoin — un correctif, une fonctionnalité attendue — ou juste avant chaque jalon. Pas de
mise à jour de confort en cours de phase : une régression amont au mauvais moment coûte des jours de
recherche pour un bug qui n'est pas le nôtre.

C'est aussi la raison pour laquelle les modifications de macemu restent des **patches** dans
`patches/macemu/` et non un fork : un patch qui cesse de s'appliquer est un signal immédiat et lisible,
là où un fork accumule silencieusement la dette de rebase.

## 5. Licence

- **Circle : GPL-3.0**
- **Basilisk II : GPLv2 *or later*** (vérifié dans les en-têtes de tous les fichiers du cœur)

L'œuvre combinée ne peut donc être distribuée que sous **GPLv3**. Le fichier `LICENSE` doit être le texte
de la GPLv3, et le README doit l'énoncer.

À maintenir : un `NOTICE.md` listant l'origine de chaque morceau repris, avec ses auteurs et copyrights.

**Position sur les ROM et les systèmes Apple.** Les ROM Macintosh 68k n'ont jamais été libérées et
**restent sous copyright Apple** ; leur statut hors possession du matériel d'origine est incertain et
dépend des juridictions. Apple a distribué System 7.5.3 gratuitement à l'époque mais ne le propose plus ;
les archives publiques (Internet Archive, Macintosh Repository) hébergent ces contenus sous la mention
d'abandonware. Certains projets voisins choisissent d'embarquer les ROM dans leur dépôt — Infinite Mac
les place dans `src/Data/`. **Okapia ne le fait pas** : rien n'est versionné, rien n'est réhébergé,
l'utilisateur fournit sa ROM, et le message d'erreur en son absence est explicite et documentaire
(§7.13). `roms/` est dans `.gitignore`.

---

## 6. Structure du dépôt

```
mac68k-rpi-circle/
├── external/
│   ├── circle-stdlib/          # sous-module (contient Circle et newlib)
│   └── macemu/                 # sous-module
├── reference/                  # cloné par script, ignoré par Git
├── src/
│   ├── circle/                 # équivalent de macemu/src/SDL/ — les cinq fichiers à écrire (§8)
│   │   ├── hal_circle.{h,cpp}  # couche matérielle, indépendante de l'émulateur
│   │   ├── main_circle.cpp
│   │   ├── video_circle.cpp
│   │   ├── input_circle.cpp
│   │   ├── audio_circle.cpp
│   │   ├── ether_circle.cpp
│   │   └── platform_circle.h
│   └── adapted/                # fichiers Unix amont adaptés, un par un, avec provenance en en-tête
├── patches/macemu/             # patches minimes, documentés
├── scripts/                    # bootstrap, build-qemu, run-qemu, build-pi, flash
├── qemu/                       # images SD de développement, configurations
├── tests/{host,qemu}/
├── docs/
├── roms/                       # ignoré
├── LICENSE  NOTICE.md  README.md  Makefile
```

---

## 7. Architecture technique

### 7.1 Mémoire

`DIRECT_ADDRESSING` : adresse Mac = adresse hôte − `MEMBaseDiff`, offset global fixé au démarrage
(`uae_cpu/basilisk_glue.cpp`). Pas de dépendance à la MMU, pas de contrainte de placement physique.
`banks` reste disponible comme mode de repli instrumenté pendant le débogage précoce.

Basilisk alloue **un seul bloc contigu** : `RAMSize + ROM_MAX_SIZE (1 Mo) + SCRATCH_MEM_SIZE (64 Ko)`,
soit **257,06 Mo**.

Circle segmente sa mémoire en deux tas qui ne se rejoignent pas : `HEAP_LOW` (sous 1 Go) et `HEAP_HIGH`
(créé seulement au-delà de 1 Go de RAM, plafonné à 2 Go). Avec 256 Mo, **tout tient dans `HEAP_LOW` sur
toutes les cartes** — y compris sous QEMU `raspi3b`, qui a 1 Go. Un seul chemin d'allocation, aucun code
conditionnel selon le modèle.

**Point d'implémentation impératif** : le tas de Circle sert les blocs de plus de 512 Ko en allocation
linéaire sur l'espace libre restant. Le bloc de 257 Mo doit être alloué **au tout début du démarrage,
avant l'initialisation des pilotes**.

Aucun modèle de Pi au-delà de 2 Go n'apporte quoi que ce soit à ce projet.

### 7.2 Vidéo

Basilisk ne demande à la plateforme que trois méthodes (`include/video.h`) :

```cpp
virtual void switch_to_current_mode(void) = 0;   // doit appeler set_mac_frame_base()
virtual void set_palette(uint8 *pal, int num) = 0;
virtual void set_gamma(uint8 *gamma, int num) = 0;
```

et la **liste des modes est construite par nous** : `VideoInit()` remplit un `vector<video_mode>` que
Basilisk publie dans la slot ROM de sa carte NuBus virtuelle. C'est donc nous qui écrivons le menu que le
tableau de bord **Moniteurs** affichera.

**Architecture retenue — framebuffer de sortie fixe et compositeur :**

```
framebuffer Mac (RAM, 1/2/4/8/16/32 bits, big-endian)
   ↓  Screen_blit() + ExpandMap[]        ← routines amont (CrossPlatform/video_blit.cpp)
   ↓  mise à l'échelle entière + centrage
framebuffer Circle FIXE (résolution native de l'écran, ARGB8888)
   ↓  HDMI
```

Le framebuffer de sortie est initialisé une fois au démarrage et ne bouge plus. Changer de résolution ou de
profondeur dans Moniteurs ne touche que le tampon Mac et le paramétrage du compositeur : **aucune
renégociation HDMI, aucun clignotement**.

Ce que cela évite d'écrire :

- **le gris et le noir et blanc sont gratuits** : le cœur applique lui-même le mapping de luminance sur la
  palette avant de nous la transmettre (`video.cpp:569`) ;
- **les conversions de pixels existent** : `Blit_Expand_{1,2,4,8}_To_{8,16,32}` et
  `Blit_RGB555/RGB888_{NBO,OBO}` — avec la gestion du big-endian — sélectionnées par
  `Screen_blitter_init()`, alimentées par la table `ExpandMap[256]` que remplit `set_palette()`.

**Coût de composition**, à 60 Hz : 640×480×8 en 1:1 ≈ 74 Mo/s ; doublé en 1280×960 ≈ 295 Mo/s ;
1024×768×8 en 1:1 ≈ 189 Mo/s. Négligeable sur Pi 4/5 ; sur Pi 3 c'est là que le suivi des zones modifiées
devient utile (M5Tab mesure 60 à 90 % d'économie).

**Table des modes** : comme le doublement entier est ce qui rend l'image nette, la table doit dépendre de la
résolution de sortie. Sur un écran 1080p, 512×384 et 640×480 se doublent proprement ; 800×600 et 1024×768
ne tiennent qu'en 1:1 centré. En faire une **donnée de configuration**, pas une constante.

**Spécificité Pi 5** : la documentation Circle est explicite — « *no configuration in config.txt, cannot set
display resolution from application* ». La résolution est imposée par le firmware. Le compositeur rend cette
limite indolore pour l'utilisateur : la seule chose perdue est le raccourci sans conversion, qui n'était
qu'une optimisation. Sur Pi ≤ 4, ce raccourci reste possible en option quand le Mac est en 8 bits.

### 7.2bis Stratégie matérielle : trois cibles, un seul code

| Cible | Rôle |
|---|---|
| **Pi 3** | cible de **simulation** — c'est le modèle qu'émule QEMU, donc celui du développement logique et de la CI |
| **Pi 4** | cible de **référence** — le tableau de fonctionnalités de Circle y est complet |
| **Pi 5** | cible de **compatibilité** — fonctionne, par le chemin générique |

La difficulté n'est pas que le Pi 5 ajoute du matériel (RP1, NVMe, RTC), c'est qu'il **retire du contrôle
à l'application** : plus de choix de résolution, plus de FIQ, plus de VCHIQ. Rien ne garantit qu'un modèle
ultérieur sera plus permissif — il faut concevoir pour cette tendance, pas contre elle.

**1. Découvrir, jamais supposer.** Les capacités d'affichage sont relevées au démarrage — résolution
imposée ou non, profondeur indexée acceptée ou non, VSync et double tampon disponibles ou non — et le
compositeur consulte ce relevé. Un matériel qui retire encore quelque chose fait basculer un booléen, il ne
fait pas réécrire un chemin. Corollaire déjà appliqué dans le test de la phase 1 : le programme affiche ce
que le firmware a **accordé**, jamais ce qu'il a demandé.

**2. Le chemin générique est le chemin principal.** La composition logicielle vers un framebuffer fixe
fonctionne partout. Les raccourcis matériels — 8 bpp avec palette matérielle, zéro-copie — sont des
optimisations optionnelles là où elles existent, **jamais des prérequis**. C'est l'inverse du plan initial,
qui faisait du raccourci l'architecture avant de découvrir qu'il n'existe pas sur Pi 5.

**3. Les `#ifdef RASPPI` restent confinés à `hal_circle`.** Circle fixe `RASPPI` à la compilation : un
binaire par modèle, c'est incontournable. Mais le code Macintosh n'en voit jamais un seul, et tout ce qui
est au-dessus de la couche matérielle compile à l'identique pour les trois cibles.

Les nouveautés propres à un modèle (RTC et NVMe du Pi 5) sont traitées comme des **bonus détectés à
l'exécution**, jamais comme des dépendances — l'horloge du §7.7 en est déjà l'exemple.

### 7.3 Cadencement et multicœur

Contraintes Circle (`doc/multicore.txt`) : **toutes les IRQ périphériques sont traitées sur le cœur 0** ;
le **scheduler coopératif ne tourne que sur un cœur, et ce doit être le cœur 0** ; `ARM_ALLOW_MULTI_CORE`
ralentit légèrement même en mono-cœur ; seules quelques classes sont utilisables depuis plusieurs cœurs, le
reste demande un `CSpinLock`.

**Stratégie retenue — S1 :**

| Cœur | Rôle |
|---|---|
| 0 | IRQ, scheduler, I/O (SD, USB, réseau), tick 60 Hz |
| 1 | **boucle d'émulation 68k**, jamais interrompue |
| 2, 3 | libres — voir S2/S3 ci-dessous |

Le tick 60 Hz et le VBL viennent d'un **timer d'interruption** (`CTimer`), pas d'une tâche du scheduler :
la couche Unix de Basilisk crée un thread pthread pour cela, ce qui n'a pas de sens sur un ordonnanceur
coopératif.

Évolutions ultérieures, **sur mesures uniquement** (§13) : **S2** ajoute la composition vidéo sur le cœur 2
(retire 3 à 10 ms par image du chemin d'émulation) ; **S3** ajoute audio et disque sur le cœur 3. La
parallélisation du cœur 68k lui-même n'est **pas** au programme.

**Réserve pour Pi 1/2/3, à écrire dans le README** : sur ces modèles l'Ethernet est un périphérique **USB**
partageant le bus avec le clavier et la souris, et Circle prévient qu'un trafic USB en masse affame les
transferts d'interruption. Un téléchargement soutenu rendra le pointeur pâteux. C'est la topologie de ces
cartes, pas un défaut du port. Sur Pi 4 et 5 le contrôleur réseau est natif et le problème disparaît. Sous
QEMU la question ne se pose pas.

### 7.3bis Le tick 60 Hz : pourquoi un accumulateur

Le 60 Hz du Mac n'est pas un réglage d'affichage, c'est **son unité de temps**. Chaque VBL incrémente la
variable `Ticks` en mémoire basse ; `TickCount()` rend des soixantièmes de seconde, et tout le système s'y
adosse — Time Manager, clignotement du curseur, temporisations applicatives. Faire battre le Mac à une
autre fréquence fausserait toutes ses horloges : à 100 Hz, il croirait le temps 1,66 fois plus rapide.

Circle bat à `HZ = 100`, valeur **codée en dur** dans `circle/timer.h` et non surchargeable depuis
`sysconfig.h`. D'où l'accumulateur de `tick_circle.cpp` : il travaille en microsecondes, chaque tick hôte
ajoutant 10 000 µs et un tick Mac partant dès que 16 625 µs sont atteints, le reste étant reporté. **Le
débit moyen est donc exact** — pas de dérive — au prix d'un jitter : des périodes de 10 ou de 20 ms.

Ce jitter n'affecte ni les horloges (la moyenne est juste) ni l'image (le compositeur est indépendant du
VBL). Il compterait pour l'audio, qui aura son propre DMA, et pour un affichage sans déchirement, qui
voudra de toute façon le **VSync réel** plutôt qu'un timer.

Repli si les mesures du §11 le réclament : patcher Circle en **`HZ = 300`**, multiple exact de 60 — un tick
Mac tous les cinq ticks hôte, sans jitter. À peser : huit fichiers de Circle dépendent de `HZ`, dont l'USB,
dont les temporisations sont exprimées en `MSEC2HZ()`. C'est un patch qui touche tout le système pour un
bénéfice local, d'où le choix de ne pas le prendre d'emblée.

**Contrainte d'implémentation** : le gestionnaire s'exécute en contexte d'interruption sur le cœur 0 et ne
fait que poser des drapeaux. Il n'est armé qu'une fois l'émulateur prêt — une interruption levée avant que
le Mac ne puisse la servir est perdue, et une première VBL perdue ne se distingue pas d'un blocage.

### 7.4 Entrées

`CUSBKeyboardDevice` et `CUSBMouseDevice` → `input_circle.cpp` → ADB Basilisk. À traiter : table de
correspondance des codes USB HID vers les codes Mac, modificateurs (Commande, Option), appui et relâchement,
mouvement et boutons de la souris, et des raccourcis de débogage interceptés avant le Mac.

Le curseur est dessiné par Mac OS dans son propre framebuffer : rien à faire de particulier.

### 7.5 Stockage

Image disque brute sur la carte SD, lue et écrite via `open`/`read`/`write`/`lseek` — fournis par
circle-stdlib au-dessus de FatFs.

Politique d'intégrité : **vidage du cache toutes les 2 secondes** (borne la perte en cas de coupure
brutale), vidage complet à l'arrêt propre, et option lecture seule pour les images de référence.

### 7.6 Dossier partagé — la fonctionnalité à fort effet

`extfs.cpp` expose un répertoire hôte comme volume Mac, et **le problème des forks de ressources est déjà
résolu de façon portable** (`Unix/extfs_unix.cpp:79-83`) : pas d'attributs étendus, deux sous-répertoires
cachés.

```
/chemin/.finf/fichier   → FInfo/DInfo (type, créateur, drapeaux Finder)
/chemin/.rsrc/fichier   → fork de ressources
```

Ce schéma fonctionne tel quel **sur FAT**, donc sur une carte SD lisible par n'importe quelle machine. Une
table `e2t_translation[]` attribue en outre le bon type et le bon créateur aux extensions courantes :
`.sit` → `SIT!`/`SITx`, `.hqx`, `.bin`, `.zip`, `.gz`, `.aiff`, `.pdf`…

Le flux visé fonctionne donc directement : déposer une archive StuffIt sur la carte depuis n'importe quelle
machine, la voir apparaître avec la bonne icône dans le Finder, la détendre avec StuffIt Expander — qui
recrée les forks à l'intérieur du volume Mac. **Plus besoin de fabriquer une image disque à chaque ajout
d'application.**

Limites à documenter : FAT n'accepte pas tous les caractères des noms Mac, et un fichier déposé depuis un PC
n'a pas de fork de ressources — d'où l'intérêt du passage par archives.

### 7.7 Horloge

Ordre de confiance : **NTP → RTC → dernière valeur enregistrée sur SD**.

Le point d'entrée est unique : `TimerDateTime()`, appelé par le trap horloge émulé. Journaliser quelle
source a servi — une machine qui repart en 1904 doit dire pourquoi. Réenregistrer l'heure sur SD à l'arrêt.

Deux nuances :

- **le RTC n'existe que sur Pi 5** (connecteur pile dédié) ; sur Pi 3 et 4 l'ordre effectif est NTP puis SD.
  Circle fournit les deux récepteurs dans `addon/rtc/` : `CFirmwareRTC::Set(const CTime &UTCTime)`, qui passe
  par les tags mailbox `PROPTAG_SET_RTC_REG` — donc le RTC du Pi 5 — et `CMCP7941X` pour un module I²C, seule
  façon d'en avoir un sur Pi 3 et 4 ;
- **Basilisk ignore les écritures dans l'horloge matérielle** : dans `emul_op.cpp:182`, la branche « écriture »
  des registres RTC se contente de journaliser. Régler l'heure dans le Mac tient pour la session mais ne survit
  pas au redémarrage.

**L'écriture se capte sans toucher à `external/`**, et le crochet existe déjà. Le trap `ClkNoMem` ($A053) est
patché en `M68K_EMUL_OP_CLKNOMEM` sur notre ROM — `rom_patches.cpp:1188`, branche « ROM23/26/27/32 », ROM32
étant la Quadra 32-bit clean — donc l'écriture passe par `EmulOp()`, que `xpram_hook_circle.cpp` enveloppe
déjà avec `--wrap=_Z6EmulOptP13M68kRegisters`. Les octets de l'horloge arrivent dans `d1` et `d2`. Trois
précautions :

- **capturer `d1`/`d2` à l'entrée du wrapper** : celui-ci appelle `__real_` avant de tester, or `EmulOp` finit
  la branche par `r->d[1] = r->d[2]` et écrase donc l'opération ;
- **reconstituer les quatre octets** avant d'appliquer : le Mac écrit les registres 0 à 3 un par un ;
- **cette valeur prime sur le plancher `drLsMod`** de `CKernel::RefineClock()`. Sans cela, un utilisateur qui
  recule volontairement son horloge se la voit remonter au démarrage suivant — le plancher protège contre une
  horloge qui recule toute seule, pas contre une décision explicite.

**Conséquence pour le firmware** (§7.12) : le réglage de l'heure appartient au tableau de bord Date et heure
du Mac, pas à une interface de démarrage. C'est period-correct, et cela supprime le double réglage et son
problème de synchronisation. `timezone` cesse alors d'être un réglage utilisateur pour devenir une conversion
interne — le Mac écrit de l'heure locale, on la relit dans le même repère, le décalage s'annule. Le fuseau ne
redevient nécessaire que le jour du NTP, et reste éditable dans le fichier de préférences.

Côté Mac, le client NTP intégré n'apparaît qu'avec **Mac OS 8.5** ; sur System 7.x → 8.1 il faut un
utilitaire tiers (Vremya, Network Time, Mac-NTP), que le dossier partagé rend facile à installer. Dans tous
les cas l'heure du **démarrage** vient de nous : le Mac lit l'horloge avant qu'aucune pile réseau ne soit levée.

### 7.8 PRAM et arrêt propre

Les écritures PRAM du Mac passent toutes par le trap horloge émulé, et il n'y a que **deux sites d'écriture**
dans tout le chemin (`emul_op.cpp`, branches « Write XPRAM » et « Write PRAM »). Inutile de sonder
périodiquement comme le fait la couche Unix :

1. drapeau `xpram_dirty` posé sur ces deux sites (**patch de deux lignes**, documenté dans `patches/`) ;
2. écriture sur SD après ~1 s sans nouvelle écriture, pour regrouper les rafales ;
3. écriture inconditionnelle à l'arrêt.

Ce sont les réglages qui doivent survivre : volume sonore, dossier Système de démarrage, disque de
démarrage, profondeur d'écran.

**Arrêt propre** : le « Shut Down » du Finder est observable côté émulateur. À ce moment : vider le cache
disque, écrire la PRAM et l'heure, puis afficher un écran d'extinction ou couper.

### 7.9 Réseau

**Montage retenu : partage de l'adresse MAC du Pi.** Les trames du Mac partent telles quelles via
`CNetDevice::SendFrame()`, avec la MAC du Pi comme adresse du Mac. Le Mac fait son propre ARP et son propre
DHCP : il devient un **vrai nœud du réseau local** — AppleShare, AppleTalk sur IP, navigateur d'époque.
Coût : nul, aucun patch de Circle. Fonctionne **aussi en WiFi**, là où un pont de niveau 2 avec une MAC
étrangère échouerait.

Contrainte : une seule pile à la fois sur l'interface. Faire le NTP au démarrage, puis céder le contrôle au
Mac.

En second recours, **slirp** (NAT en espace utilisateur) est déjà dans l'arbre macemu (`src/slirp/`,
13 000 lignes) et n'utilise que quelques appels système — que circle-stdlib fournit, `select()` compris.
Utile pour isoler le Mac ou lui donner un accès sortant sans le poser sur le LAN.

Le pont de niveau 2 avec MAC distincte est écarté : `CNetDevice` n'expose pas de mode *promiscuous*, et ce
mode est inopérant en WiFi.

### 7.10 Audio

`CHDMISoundBaseDevice` (sans VCHIQ), disponible sur toute la gamme y compris Pi ≤ 3. I2S en alternative.
À traiter : taille des tampons, fréquence d'échantillonnage, DMA, sous-alimentation et débordement, latence.

### 7.11 ROM

**Une seule ROM de référence** tant que le système n'est pas stable. Jamais versionnée (`roms/` est ignoré).

**Validation outillée** : `scripts/check-rom.py` contrôle taille, somme de contrôle et mot de version avant
tout démarrage. Basilisk n'accepte une ROM que si le mot 16 bits big-endian à l'offset 8 vaut **`0x067C`**,
marqueur « 32-bit clean » obligatoire en `DIRECT_ADDRESSING` (`rom_patches.cpp:838`). La somme de contrôle
est le mot long de tête, égal à la somme de tous les mots de 16 bits depuis l'offset 4.

Deux ROM Quadra 650 sont disponibles localement, toutes deux vérifiées bonnes (1024 Ko, somme conforme,
`0x067C`). Malgré des noms de fichiers voisins, elles proviennent de machines différentes :

| Somme | Origine réelle | |
|---|---|---|
| **`F1ACAD13`** | `Quad650.ROM` — **Quadra 650** | **retenue** : celle que la communauté Basilisk II recommande |
| `F1A6F343` | `Quad610.ROM` — ligne **Centris 610** | repli |

**L'identité de la machine ne dépend pas de la ROM — et ne doit surtout pas en être déduite.**

La préférence `modelid` est écrite dans le champ `productKind` de la ROM chargée
(`rom_patches.cpp:1036`) et vaut « Gestalt Model ID moins 6 ». C'est ce que le Mac lit via Gestalt pour
savoir quelle machine il est. Juste au-dessus, Basilisk **désactive les slots NuBus** dans la même
structure `UniversalInfo` : la machine émulée n'est pas un Quadra 650, c'est une machine synthétique dont
on choisit l'étiquette. La ROM fournit le code, pas l'identité.

Basilisk n'expose que deux valeurs, et le critère est **la version de Mac OS visée** :

| `modelid` | Machine annoncée | Pour |
|---|---|---|
| **`5`** | Mac IIci | **retenu tant qu'on travaille sous System 7** |
| `14` | Quadra 900 | Mac OS 8.x, à reprendre quand la cible 8.1 reviendra |

**Vérifié le 2026-08-29**, et la conséquence n'avait pas été tirée. Avec `modelid 14`, un System 7.1
muni de son *System Enabler 040* s'arrête sur un cadre « Welcome to Macintosh » vide : l'enabler contrôle
l'identifiant machine, et on lui annonce un Quadra 900 qu'il ne couvre pas. Avec `modelid 5`, le même
volume démarre jusqu'au Finder. Le 7.6 démarre indifféremment avec l'une ou l'autre valeur.

Donc **le `modelid` suit le Système installé, pas la ROM** — et deux Systèmes de familles différentes ne
peuvent pas cohabiter sur une même configuration. C'est à l'interface de la phase 16, qui laissera choisir
le Système avant démarrage, de poser le `modelid` assorti. En attendant on reste sous System 7, donc `5`.

D'autres valeurs fonctionnent parfois mais ne sont pas supportées : un `29` (Quadra 800) ne rend le
*System Profiler* correct sous 8.1 qu'avec la ROM Quadra 800 assortie, toute autre combinaison le cassant.
Hors des deux valeurs sûres, il faut une cohérence ROM ↔ `modelid`, payée en fragilité.

**Déduire le `modelid` de la ROM serait donc un piège** : avec une ROM Quadra 650, la déduction donnerait
l'identifiant d'un Quadra 650, et Mac OS 8 pourrait refuser de démarrer faute de la valeur validée.

Au chargement, produire un message explicite (« ROM absente / non reconnue / non 32-bit clean ») plutôt
qu'un plantage.

### 7.12 Le firmware Okapia — un « Open Firmware qui aurait pu exister »

Les Mac 68k n'ont jamais eu d'Open Firmware : il est arrivé avec les Power Mac. Ce que la machine offrait,
c'était des combinaisons de touches au démarrage et des tableaux de bord. **Okapia comble ce vide avec ce
qui aurait pu exister** : un utilitaire de configuration à l'esthétique Apple — plausible, sans être celle
d'un Système en particulier —, en noir et blanc, piloté au clavier et à la souris, et qui s'ouvre **avant**
que le moindre code Basilisk ne s'exécute.

Ce qu'il remplace : la configuration par fichier texte. Le fichier de préférences reste la source de
vérité — le firmware ne fait que l'éditer, ce qui garde les deux voies cohérentes.

**Le critère d'admission.** Un réglage mérite sa place ici s'il réunit quatre conditions : l'invité ne sait
pas le faire lui-même, le besoin apparaît **après** la fabrication de la carte, le code Okapia le consomme
vraiment, et — le point décisif — il **débloque une machine qui refuse de démarrer**. Partout ailleurs
l'alternative est d'éteindre, de sortir la carte SD, de la monter sur un autre ordinateur et de recommencer ;
c'est ce coût-là que le firmware existe pour supprimer, et c'est lui qui arbitre. Profondeur d'écran, volume
sonore et disposition clavier échouent au premier test : le Mac a des tableaux de bord pour ça.

**Ce qu'il permet** : choisir parmi les systèmes détectés sur les disques. Pour chaque système, Okapia
détermine automatiquement le modèle de Macintosh à annoncer ; il ne présente jamais de « profil » à
l'utilisateur, et il n'y a **qu'une ROM** (§7.11), donc rien à choisir de ce côté. Une colonne de boutons
radio désigne l'unique système par défaut ; ce choix
place son entrée `disk` avant les autres dans `BasiliskII_Prefs`, indépendamment du système sélectionné
pour ce démarrage. **L'état actuel du code n'est pas un filtre pour l'interface cible** : une valeur utile
aujourd'hui en dur reste prévue comme préférence, avec son implémentation inscrite dans la phase
correspondante.

Sur chaque ligne, deux cases qui n'existent aujourd'hui que dans le fichier :

- **lecture seule**, c'est-à-dire le préfixe `*` que `disk.cpp:161` comprend déjà. C'est le geste de sûreté
  du projet — un volume de secours qu'aucun arrêt brutal ne peut salir — et il coûte un caractère ;
- **monter ou non**. Une image présente sur la carte mais absente des préférences n'est montée que par le
  repli de `kernel.cpp:455`, quand rien d'autre n'existe : ajouter un disque de données, ou écarter un
  volume suspect, impose donc aujourd'hui de sortir la carte.

La ligne affiche aussi l'**état du volume**, propre ou laissé en cours d'utilisation, ce que `HfsInspect()`
sait déjà dire. C'est ce qui prépare le dialogue de réparation.

**Un bouton de réglages** porte le glyphe du panneau Preferences repris de la maquette. C'est un relevé, donc
une exception assumée à la règle « le chrome ne cite aucun système » — décidée après l'avoir vu à l'écran, et
notée ici pour que la règle et le code ne se contredisent pas en silence. Les deux autres glyphes du chrome,
l'arrêt et la puce mémoire, sont dessinés pour le projet. Le bouton édite uniquement `BasiliskII_Prefs` :
`ramsize`, `frameskip`, `extfs`, `extfsname`, le son et la langue de l'interface. Deux d'entre eux ne sont
pas ce qu'ils paraissent :

- **le son n'est pas un booléen.** `audio_circle.cpp:70` construit en dur un `CPWMSoundBaseDevice`,
  c'est-à-dire la prise jack — que le Pi 5 n'a pas, et qui est la mauvaise sortie sur un téléviseur. Circle
  offre aussi `CHDMISoundBaseDevice`, `CI2SSoundBaseDevice` et `CUSBSoundBaseDevice`, donc le réglage utile
  est **coupé / HDMI / jack / USB**. À trancher en phase 14 sur matériel, mais réservé dès maintenant. Et
  `nosound` reste par ailleurs une **soupape** : revendiquer un périphérique que l'invité ne peut pas
  entendre l'a déjà gelé une fois, et pouvoir le couper ici répare ce cas sans démontage ;
- **`extfsname` est le seul champ de saisie libre** du firmware — curseur, retour arrière, 27 caractères au
  plus parce que le nom finit dans une chaîne Pascal du *volume record*. Il le mérite : c'est un volume que
  l'utilisateur voit sur son bureau, donc son nom lui appartient. Aucun redémarrage n'est requis, le menu
  tournant avant Basilisk. À côté, une **ligne de diagnostic** plutôt qu'un silence : le firmware connaît la
  version du Système de chaque volume, il peut donc dire « Système 7.1 : nécessite l'extension File System
  Manager 1.2 » au lieu de laisser l'absence du volume partagé passer pour une panne.

**Trois boutons qui ne sont pas des réglages** et qui portent une bonne part de la valeur de l'écran :

- **Éteindre.** Toute la doctrine du projet est de ne jamais tirer la prise, et la seule sortie du menu sans
  démarrer le Mac est aujourd'hui la coupure de courant.
- **Oublier la PRAM**, et la combinaison historique avec elle. `main.cpp:106` réinitialise le XPRAM dès que
  la signature « NuMc » manque : supprimer `/BasiliskII_XPRAM` (`xpram_circle.cpp:31`) *est* le zap, sans
  jamais toucher aux données. Cmd-Option-P-R pendant la fenêtre de démarrage est **une exception documentée**
  à la règle qui envoie au Mac les autres combinaisons avec Option — un vrai Macintosh fait ce zap dans sa
  ROM, avant que l'ADB émulé ne soit alimenté, donc laisser passer la combinaison ne marcherait pas. Rejouer
  le carillon une seconde fois le jour où §7.13 l'aura, comme le faisait la machine.
- **Informations.** ROM trouvée et modèle retenu, taille et espace libre de la carte, volumes et leur état,
  volume de démarrage, date de compilation, modèle de Pi, et **le mode que le firmware a réellement accordé**.
  Tout cela n'existe aujourd'hui que sur l'UART — un fil que personne n'a quand la carte est dans le Pi sous
  un téléviseur. C'est exactement le problème du fichier de préférences : de l'information prisonnière d'un
  accès physique. Le `modelid` et son échec de détection éventuel vont là, pas dans les réglages.

**Le dialogue de réparation** est la première vraie raison d'être de cet écran : aujourd'hui `HfsRepair`
scavenge en silence au démarrage, et c'est le seul endroit du système qui écrit dans le volume de
l'utilisateur sans qu'il l'ait demandé. Il doit pouvoir le voir et le refuser — mais aussi l'accepter une
fois pour toutes, ce qui fait trois états et non deux : réparer sans rien demander, demander, ne jamais
réparer. Trois contraintes le cadrent :

- **ne pas changer le type de `hfsrepair`**, déclaré `TYPE_BOOLEAN` (`prefs_circle.cpp:57`). Le parseur lit
  selon le type déclaré : le passer en entier ferait lire `0` sur toutes les cartes existantes, sans un mot.
  Le troisième état passe par un second booléen, `hfsrepairask` ;
- **le dialogue expire**, décompte à l'appui. Un appareil sans personne au clavier transformerait sinon un
  volume réparable en blocage — l'inverse exact de ce que ce firmware existe pour faire ;
- **l'expiration répare**. Autrement `scripts/run-test.sh` s'arrête sur le dialogue, lui qui redémarre la
  carte précisément pour que `HfsRepair` tourne avant de juger le volume.

Reste une tension à trancher au moment de l'écrire, pas à masquer : la phase 18 veut une mise sous tension
qui n'affiche rien. Le défaut proposé est « demander », parce qu'un volume à réparer n'est pas le chemin
normal et que l'interruption est donc rare ; `hfsrepairask false` rend l'appareil silencieux.

**Ce qui n'apparaît jamais**, sous peine de faire mentir l'interface : `screen`, `displaycolordepth`, `title`,
`hotkey`, `scale_*`, `sdlrender`, `nogui`, `jit*`, `scsi*`, `ether*`, `seriala`/`serialb`, `dsp`, `mixer`,
`ignoresegv`, `xpram`, `idlewait`, `bootdrive`/`bootdriver`, `yearofs`/`dayofs`, `keyboardtype`, `floppy`,
`cpu`, `fpu`, `hfsinventory`. Le README les classe déjà comme sans effet ici, comme invariants (§2) ou comme
verbosité de journal.

Certaines modifications peuvent exiger un redémarrage complet d'Okapia. L'interface les marque d'un `*`
et remplace alors « Enregistrer » par « Enregistrer et redémarrer » ; ce coût d'application ne justifie
jamais de supprimer la préférence. Dans l'ordre d'initialisation actuel, `ramsize` est le premier cas : la
mémoire Mac est allouée avant l'USB qui rend le menu utilisable. Ce n'est pas déclaré immuable — une future
préallocation sûre pourrait retirer cette contrainte. `nosound` apparaît dès la conception même si son
effet matériel complet appartient encore à la phase 14.

**Pas de réglage de résolution.** `CBcmFrameBuffer(0, 0, 32)` demande au firmware les dimensions du display
sélectionné par `PROPTAG_GET_DISPLAY_DIMENSIONS` : pour HDMI, le firmware a déjà choisi le mode à partir de
l'écran et de sa configuration ; pour l'écran DSI officiel, il connaît de même le panneau qu'il pilote.
Okapia ne négocie donc pas lui-même HDMI ou MIPI DSI, il consomme le mode retenu. Il filtre ensuite sa table
de modes Macintosh pour ne garder que ceux qui tiennent dans cette sortie ; le tableau de bord Moniteurs et
la PRAM du Mac possèdent le mode logique actif — §7.8 compte déjà la profondeur d'écran parmi ce que la PRAM
conserve, donc il n'y a pas non plus de « mode par défaut » à régler ici.

Deux précisions qui closent la question. **QEMU ne prouve rien sur le mode** : son « équivalent moniteur »
est une propriété de device, que `scripts/screenshot.sh:28` fixe avec `-global bcm2835-fb.xres=/yres=`. Il
valide le chemin mailbox, pas la négociation, puisqu'il n'y a ni EDID ni écran. Et **un forçage manuel serait
asymétrique** : sur Pi 3 et 4, `CBcmFrameBuffer` accepte une taille explicite et il marcherait ; sur Pi 5 la
résolution ne se règle ni depuis l'application ni depuis `config.txt`, et il ne ferait rien. Un réglage qui
fonctionne chez un utilisateur sur deux est pire que pas de réglage. Un forçage ne rejoindra donc que les
diagnostics, et seulement si un essai matériel prouve un cas de détection défaillante. Si plusieurs sorties
sont présentes, le choix utile éventuel serait **la sortie** (HDMI0, HDMI1 ou DSI), pas sa résolution ;
ne l'ajouter qu'après
essai matériel. Le réseau reste aussi hors de l'interface : un simple activé/désactivé n'apporte rien ; la
phase 13 le réévaluera si un vrai choix de mode ou d'interface apparaît. Le même firmware provisionne un
système (§7.14) et affiche les diagnostics quand quelque chose manque.

**Déclenchement** : dès que le clavier est utilisable, Okapia affiche environ deux secondes un fond gris
uni, comme le faisait un Macintosh à écran couleur avant son Happy Mac. `Option` seule ouvre le sélecteur ;
sans elle, le système par défaut démarre. Le sélecteur
s'ouvre aussi automatiquement lorsque la configuration est absente ou invalide, ou qu'aucun système
n'est amorçable. Les autres combinaisons comprenant `Option` restent destinées au Macintosh émulé.

**Faisabilité : `C2DGraphics`, pas LVGL.** Le cœur de Circle porte déjà l'essentiel, sans le moindre addon :
`lib/2dgraphics.o` est dans `libcircle.a` et `C2DGraphics` offre une surface double-tamponnée avec VSync,
`DrawRect`, `DrawRectOutline`, `DrawLine`, `DrawImage`, `DrawImageTransparent`, `DrawPixel`, `DrawText`,
`GetBuffer` et `UpdateDisplay` — son `SetPixel` sait même écrire en 1 bpp, et `CCharGenerator::GetPixelLine()`
rend les bits d'une rangée de glyphe, ce qui est le point d'accroche du blitter proportionnel.

Mais il faut mesurer l'écart avec ce qu'on remplace : **`C2DGraphics` est un peintre de framebuffer, QuickDraw
était un modèle graphique.** Il n'a ni `GrafPort`, ni régions, ni motifs 8×8, ni modes de transfert, ni
`FrameRoundRect`, ni mesure de texte. Les régions ne nous manqueront pas — §7.12 interdit déjà la
superposition, et c'était la partie la plus difficile de QuickDraw. Les motifs non plus, pour une raison
qui vaut d'être dite (voir plus bas). Restent **trois** manques, quelques dizaines de lignes chacun :
l'inversion pour la vidéo inverse — par `GetBuffer()`, faute de mode `patXor` —, le roundrect parce que le
bouton Apple en est un depuis 1984, et la mesure de texte parce que la proportionnelle l'exige.

**Donc on bâtit à côté de `C2DGraphics`, pas dessus.** Nos primitives prennent une *surface* — pointeur,
largeur, hauteur, pas, profondeur — et non un `C2DGraphics`. Sur le Pi cette surface vient de `GetBuffer()` ;
**sur l'hôte c'est un tampon ordinaire**, et chaque composant dans chaque état se rend alors en fichier, se
regarde et se compare d'une version à l'autre. L'AGENTS.md demande des tests sur l'hôte quand c'est possible :
ici ça l'est, et c'est ce qui fait passer « thème reproductible » d'une intention à une propriété vérifiée.
`C2DGraphics` garde ce qu'il fait bien — cycle de vie du tampon, double tampon, VSync, `UpdateDisplay` — et
`TFont`/`CCharGenerator` fournissent les glyphes.

Le compte honnête de ce que LVGL aurait coûté, puisque c'était la voie prévue :

- `CLVGL` fait `assert (m_pDisplay->GetDepth () == LV_COLOR_DEPTH)` et le `lv_conf.h` de `external/` fixe
  `LV_COLOR_DEPTH 16` quand notre sortie est en 32 : il aurait fallu notre propre configuration et notre
  propre compilation des 459 `.c` de LVGL, 24 Mo de sources ;
- `CLVGL` ne câble que la souris et l'écran tactile — **aucun périphérique clavier**, donc `lv_indev` KEYPAD
  et groupes de focus à écrire de toute façon ;
- il utilise la souris *cooked* alors que `input_circle.cpp` a pris la voie *raw* pour l'émulateur, d'où une
  bascule à gérer au passage de l'un à l'autre ;
- son thème monochrome vise l'e-papier : notre chrome — filets d'un pixel, ombre à décalage net, boutons
  radio en pixel art — aurait été redessiné entièrement.

L'argument qui restait à LVGL était la phase 17 : arborescence, barre de progression, saisie. Une barre de
progression et un arbre posés sur la liste que nous aurons déjà écrite ne justifient pas cette dépendance ;
la question se rouvrira le jour de la phase 17, pas avant.

**Le 1 bit est une discipline de palette, pas un format de pixel** — et cette discipline s'arrête où le
matériel de 1984 s'arrêtait. **Le noir et le blanc portent la structure** : traits, texte, vidéo inverse.
C'est là qu'est la signature Apple, et elle survit à l'agrandissement puisque ce sont des lignes et non de
la texture. **Partout où l'époque tramait, un gris plein** — le fond, la glissière d'ascenseur, les états
désactivés. Non par fidélité au System 7 — il tramait, voir plus bas — mais par nécessité d'échelle : notre
agrandissement entier transforme un damier d'un pixel en carrés de 3×3 qui grouillent, et beaucoup de
téléviseurs le font en plus scintiller dans leur rééchelonnement.

**L'histoire, et où Okapia se place dedans.** La trame n'a pas disparu avec la couleur : vérifié par capture
sur un Quadra 650 démarrant System 7.1 sous Basilisk avec la ROM Quadra, et sous SheepShaver en Mac OS 8.5 —
damier dans les deux cas, jusqu'à la boîte « Bienvenue ». La raison est mécanique : ce fond est un
remplissage de **motif** QuickDraw (`PAT`), peint en avant-plan et arrière-plan, donc en noir et blanc quelle
que soit la profondeur de l'écran. Rien n'a jamais « choisi » la trame sur les Mac couleur ; QuickDraw ne
savait pas y substituer un niveau de gris.

C'est la génération suivante qui a tranché : le **Startup Manager** des Power Mac, iMac et iBook — celui qui
s'ouvre en tenant Option, et dont Okapia est très exactement le pendant 68k — pose un aplat. Sa teinte,
gris bleuté de mémoire, n'est pas vérifiée et rien ici n'en dépend : ce qui compte est l'aplat.

**Okapia se place entre les deux, et c'est cohérent avec ce qu'il est.** Ce firmware est un anachronisme
assumé — l'idée qu'Apple a eue plus tard, ramenée sur une machine 68k. Que son fond appartienne à l'époque
du sélecteur plutôt qu'à celle du Système qu'il lance n'est donc pas une infidélité, c'est la même
anachronie que tout le reste. On prend l'aplat de la génération suivante, à la **luminosité du damier de la
précédente**.

Sur la valeur, et c'est là que se joue l'entre-deux : un damier noir et blanc se moyenne dans la **lumière
linéaire**, donc son équivalent perçu n'est pas `#808080` mais environ **`#BCBCBC`** — 0,5 linéaire vaut 188
en sRGB. C'est pourquoi le bureau System 7 paraît clair, et c'est la valeur de départ. Neutre et non bleutée :
on aligne la clarté sur le damier, pas la teinte sur le Startup Manager.

Ce que l'aplat n'achète pas, pour que personne ne s'y trompe : l'écran de l'invité passe par le même
agrandissement entier, donc **son** damier grouillera pareillement sur un 1080p, pendant tout le démarrage
et tant que le bureau reste visible. On ne l'épargne pas à l'utilisateur ; on choisit pour la seule surface
qu'on possède, celle où il lit du texte et fait un choix.

Écartée délibérément : garder le damier quand le facteur d'échelle vaut 1 et l'aplat au-delà. Cela rendrait
le thème dépendant de la sortie, c'est-à-dire l'exact contraire de la propriété qu'on vient d'établir.

On dessine donc à la profondeur de la sortie, en noir, blanc et gris pleins : ni conversion, ni palette, ni
question de support I1.

**Et il n'y a pas de toile logique du tout.** C'était le plan — dessiner en 640×480 puis agrandir d'un
facteur entier — et c'était une erreur, corrigée le 2026-09-01. Agrandir transforme chaque pixel en bloc
N×N : le texte, les courbes et les filets pareillement, et l'escalier d'un arrondi n'est alors pas un défaut
de tracé mais un pixel de 640×480 vu de près. Antialiaser la toile avant de l'agrandir n'y change rien non
plus, cela ne produit que des blocs gris.

**L'interface est donc tracée à la résolution de l'écran**, avec les métriques du thème multipliées par un
facteur qui peut être fractionnaire — 36/16 sur un 1920×1080, mesuré. Un rayon de 6 devient un rayon de 13
avec treize pixels d'arc, réellement lisse ; la fonte est prise sur l'échelle à la taille voulue au lieu
d'être grossie ; et le dialogue occupe l'écran au lieu de flotter dans un îlot de 1280×960. **C'est
l'architecture qui le permet** : les métriques vivant dans le thème, un facteur d'échelle n'a qu'un seul
endroit où s'appliquer, `ThemeMake()`.

Une seule chose reste agrandie, et il faut le savoir : **les icônes**. Ce sont des bitmaps sans version plus
grande, donc elles prennent un facteur entier là où tout le reste prend le vrai. À grande échelle elles
détonnent contre un texte net — c'est le prochain actif à produire, pas un défaut de la mécanique.

**Le thème est une uchronie, pas une reproduction.** Ce menu s'ouvre devant des Systèmes qui vont de 6 à 9.
Copier le chrome de l'un d'eux le daterait contre son propre contenu et le ferait paraître faux devant les
autres. **On ne mesure donc aucun système** ; on dessine une ligne Apple *plausible* — reconnaissable sans
être située.

Ce qui porte « Apple » sans dater, et c'est peu de choses :

- **le noir et le blanc**, qui font déjà tout le travail historique à eux seuls ;
- **le rectangle arrondi**, forme du bouton Apple sans interruption de 1984 à aujourd'hui. Le rayon est une
  métrique du thème, choisie, pas une valeur relevée sur une capture ;
- **le double cerclé du bouton par défaut**, celui qu'active Retour. Continu lui aussi — liseré épais hier,
  bouton accentué aujourd'hui. On garde le liseré : c'est la touche uchronique, et il sert deux fois, le
  focus clavier reprenant la même idée ;
- **la grammaire du dialogue** : marges larges, titre centré, une action claire en bas à droite. Inchangée
  depuis quarante ans ;
- **le plat**, et c'est l'argument le plus fort. Le relief est ce qui date le plus vite — les biseaux
  Platinum sont la signature de 1997 — alors que l'absence de relief était la contrainte de 1984 et se
  trouve être la mode d'aujourd'hui. **Le plat est l'intersection exacte des deux bouts de l'histoire.**

Ce qui date, donc à éviter : biseaux et dégradés Platinum, barres de titre rayées, gel Aqua, ombres floues.
Une ombre portée à décalage net reste neutre, et bon marché.

Vocabulaire retenu : traits d'un pixel sur des champs blancs généreux ; sélection en aplat noir et texte
blanc ; case carrée, bouton radio rond ; ascenseur plat **sans flèches**, parce que les flèches situent en
1990 ; une seule fonte proportionnelle, deux tailles, du gras pour les titres et le bouton par défaut,
jamais d'italique dans le chrome.

**Ce sont les icônes qui portent l'époque, pas le chrome.** Les dossiers Système 6, Système 7 et Mac OS 8/9
de la maquette sont datés exprès, chacun désignant son volume ; le cadre qui les entoure ne l'est pas. C'est
cette répartition qui rend le menu crédible devant n'importe lequel d'entre eux. La maquette de `boot-menu/`
va déjà dans ce sens et sert de point de départ, sous réserve des corrections plus haut — plus une : ses
en-têtes de colonne tramés deviennent un aplat gris clair.

Conséquence sur la méthode, et elle est importante : sans référence à copier, **l'écran de spécimen n'est
plus un confort mais le seul arbitre**. Il n'y a rien contre quoi vérifier sinon l'œil, donc il faut pouvoir
regarder toutes les pièces ensemble, et les regarder tôt.

**L'architecture de l'interface : une seule couture.** Les composants s'écrivent **avant** les écrans, et
la raison n'est pas l'économie de code. Un thème n'existe que s'il y a exactement un endroit par lequel tous
les pixels passent ; un seul bouton dessiné ailleurs et la promesse tombe. La contrepartie est ce qui rend
l'investissement rentable : **corriger l'aspect d'un composant le corrige partout où il sert**, du sélecteur
de démarrage au dialogue de réparation et jusqu'à l'arborescence de la phase 17, sans avoir à se souvenir de
la liste des endroits.

Le piège, et c'est celui qu'Apple a mis dix ans à corriger : **les métriques appartiennent au thème autant
que les pixels.** Si le composant décide qu'un bouton fait vingt pixels de haut avec huit de marge, le thème
n'est plus remplaçable — ni plus généreux, ni plus compact — et la première traduction casse la mise en page.
Hauteur de bouton, marges, largeur d'ascenseur, retrait du liseré de focus, interligne : le composant demande,
il ne suppose pas. C'est `GetThemeMetric` à côté de `DrawThemeButton`, et c'est à prendre tel quel.

Quatre couches, et la règle de chaque frontière :

1. **Surface** — mémoire, dimensions, pas, profondeur. Rien d'autre.
2. **Primitives** — rectangle, cadre, ligne, inversion, roundrect, blit de glyphe avec styles
   synthétiques, mesure de chaîne. *Elles seules touchent la mémoire de la surface.*
3. **Thème** — une structure constante de fonctions et de métriques : `DrawButton`, `DrawCheckbox`,
   `DrawRadio`, `DrawScrollbarPart`, `DrawDialogFrame`, `DrawListRow`, `DrawFocusRing`, et à côté
   `ButtonHeight()`, `ScrollbarWidth()`, `Margin()`, `LineHeight()`. *Le thème seul appelle les primitives ;
   il ignore l'état de l'application et les événements.*
4. **Composants et écrans** — un composant est une petite donnée, pas un enregistrement à vingt champs ; un
   écran est un tableau statique de composants et une boucle d'événements. *Un composant ne dessine jamais,
   il appelle le thème.*

C'est la leçon du Dialog Manager et du `LDEF`, débarrassée de ce qui n'était qu'un artefact des ressources
de code 68k — plus de répartition sur un entier de message, plus de chargement à la demande, une structure
de pointeurs de fonctions suffit. Quatre écarts assumés avec l'époque, tous rentables :

- **la mise en page se calcule.** Le `DITL` portait des rectangles absolus, et c'est précisément pourquoi la
  localisation cassait les dialogues Mac. Avec deux langues et une fonte dont on ne fixe pas les métriques,
  un modèle de boîtes en lignes et colonnes avec mesure du texte supprime la classe entière du problème ;
- **dessin immédiat, arbre unique.** Tout redessiner à cette échelle est gratuit : ni régions sales, ni
  invalidation. L'époque ne pouvait pas se le permettre ;
- **le focus clavier fait correctement** — Tab et Shift-Tab, flèches dans les listes, Espace et Retour pour
  activer, Échap pour annuler, liseré visible. System 7 le faisait mal ; pour une interface d'avant-démarrage,
  où la souris peut n'être pas branchée, c'est décisif ;
- **une liste à rappel de ligne**, et non trois listes. Les systèmes, les réglages et l'arborescence de la
  phase 17 sont le même composant avec trois fonctions de dessin de ligne.

Le jeu minimal déduit des écrans connus : cadre de dialogue et cadre d'alerte, étiquette avec repli à la
ligne, bouton et bouton par défaut, bouton à icône, case à cocher, bouton radio, liste à rappel de ligne,
ascenseur, menu local, champ de saisie, barre de progression, liseré de focus. Onze pièces, dont la moitié
tient en vingt lignes. **Ordre d'écriture** : surface et primitives, un thème, les composants, puis un
**écran de spécimen** qui montre chaque composant dans chaque état — c'est lui qui rend la promesse
vérifiable, à l'écran comme en fichier sur l'hôte. Le sélecteur de démarrage vient en dernier, monté sur des
pièces déjà vues à l'œuvre.

Le firmware s'appuie sur le même `hal_circle` que l'émulateur (§3.5). L'interface reste un dialogue fixe
centré : aucune barre de titre, fenêtre déplaçable, superposition, animation ou gestionnaire de fenêtres.
La référence de conception est dans `boot-menu/`, avec deux réserves à corriger avant de s'en servir comme
spécification — elle montre une colonne « ROM détectée » par ligne, ce qui promet un support multi-ROM qui
n'existe pas (Okapia a une ROM et deux `modelid` sûrs, §7.10-7.11 : la colonne doit porter le modèle déduit
et la ROM va aux informations, une fois), et elle est rendue avec une police vectorielle antialiasée, donc
elle montre la mise en page et non le résultat. Son espacement est à revalider avec la vraie fonte, les
lignes à deux niveaux étant le point de rupture. Ses icônes de dossier sont des relevés d'artwork Apple :
leur distribution reste une décision distincte du GPLv3 du dépôt.

**Typographie.** **Chicago est une police Apple sous copyright** : ne pas l'embarquer, la règle ne bouge pas.
Mais elle ne bloque plus rien, parce que `libcircle.a` livre déjà sept fontes bitmap, `Font6x7` à `Font12x22`.
Cinq couvrent ISO-8859-1 jusqu'à 0xFF — `Font8x8`, `8x10`, `8x12`, `8x14`, `8x16` — donc les accents français,
capitales comprises ; `Font6x7` s'arrête à 0x80 et `Font12x22` à 0x7E, ce qui les élimine. `lib/font8x14.cpp`
est la console `lat1-14` du paquet `kbd` de Linux, sous **GPLv2+**, compatible avec le GPLv3 du projet.

Elles ne suffisent pourtant pas pour la cible, et il faut dire pourquoi : elles sont **toutes à chasse fixe
de 8 px**, et ce sont des découpes d'un même dessin — le `C` de `Font8x16` est celui de `Font8x14` avec une
rangée de plus. Or un dialogue Macintosh était proportionnel, Chicago pour les titres et Geneva pour les
listes. C'est la chasse fixe, bien plus que la taille des pixels, qui fait lire « terminal » au lieu de
« Macintosh ».

**Fait le 2026-09-01 : les Helvetica bitmap X11 sont dans `assets/fonts/`.** Geneva était en substance une
Helvetica ajustée à une petite grille de pixels, donc c'est ce qui s'en approche le plus parmi ce qui est
redistribuable. La licence a été lue dans le fichier : Adobe Systems 1984-1989/1994 et Digital Equipment
1988/1994, « permission to use, copy, modify, distribute and sell … without fee » avec clause de
non-endossement — compatible GPLv3, la réserve est levée.

Le format BDF est du texte, donc **aucun rasteriseur ni dépendance de compilation** : `scripts/gen-font.py`
le lit lui-même, comme `gen-keycodes.py` lit les codes clavier. `scripts/fetch-fonts.sh` récupère et
`scripts/trim-bdf.py` élague aux caractères affichables — 380 Ko versionnés au lieu d'un mégaoctet et demi
de cyrillique et de mathématiques. Conséquences :

- l'avance vient du `DWIDTH` et l'approche de la boîte du glyphe, donc **l'espacement est celui du
  dessinateur** et non le nôtre ;
- le **gras est une vraie graisse**, plus un étalement d'un pixel. Le gras synthétique reste dans les
  primitives comme repli, et il a coûté un bug qui vaut d'être retenu : il élargissait le glyphe sans
  élargir l'avance, donc chaque lettre grasse mordait la suivante — et `GfxTextWidth` ajoutait bien le
  pixel au total mais pas à chaque avance, si bien que même la mesure mentait ;
- **`œ`, `Œ`, `…`, `’`, les tirets et les guillemets sont dans la fonte**, au-delà de Latin-1 : les
  traductions n'ont plus rien à normaliser, et `GfxText` décode l'UTF-8 pour y accéder ;
- **plusieurs tailles sont compilées**, en échelle : cellules de 12, 14, 16, 20, 27 et 35 pixels, deux
  graisses chacune. C'est ce qui rend possible le point suivant.

**Le firmware possède le `modelid`.** C'est sa place naturelle : le champ `productKind` est patché au
chargement de la ROM (§7.11), donc avant que Basilisk ne démarre — impossible à changer une fois Mac OS
lancé. L'utilisateur choisit seulement un système détecté ; la ligne affiche pour information le modèle
qu'Okapia a retenu automatiquement.

Portée réelle du problème, à ne pas surestimer : **`14` convient de System 7.5 à Mac OS 8.1**, soit
l'essentiel de la cible. Seuls les System antérieurs à 7.5 imposent `5`. Un défaut à `14` est donc juste
presque toujours.

**Détection automatique : faite, et les deux niveaux.** Ce paragraphe annonçait un travail à repousser ;
la phase 15bis l'a livré, donc l'interface n'a plus rien à déduire elle-même :

- **le nom du volume** vient de `hfs_vstat()` par `HfsDescribe()`/`HfsInventory()`, avec la taille, l'espace
  libre, le CNID du dossier béni et l'état propre/sale (`hfs_volume_circle.h`) ;
- **la version du Système** aussi : `HfsSystemVersion()` (`hfs_volume_circle.cpp:441`) parcourt le catalogue
  jusqu'au fichier `System` du dossier béni, ouvre son fork de ressources et décode la ressource `'vers'`,
  en lecture seule.

La phase 16 est donc l'habillage de ce que la phase 15bis a découvert, pas sa fondation.

**Calendrier** : après M6. Ce n'est pas un préalable au boot de Mac OS, et le construire trop tôt
retarderait le cœur. Mais la structure doit l'admettre dès maintenant, d'où `hal_circle`.

### 7.13 Séquence de démarrage : carillon, Happy Mac, Sad Mac

L'objectif est qu'à la mise sous tension, **on reconnaisse un Mac avant de reconnaître un Raspberry Pi**.

```
mise sous tension
   ↓  (le plus tôt possible)
carillon de démarrage          ← extrait de la ROM fournie par l'utilisateur
   ↓
Happy Mac                       ← tout est en place
   ↓
Basilisk II → Mac OS

           ou, en cas de problème :

Sad Mac + code d'erreur + carillon de mort
```

**Le carillon est en ROM** : les Macintosh stockent leurs sons de démarrage et de plantage dans la ROM, sous
forme de ressources `snd `, et il est donc possible de les en extraire. Le Quadra joue une version plus
douce et plus grave de l'arpège majeur. Conséquences pratiques :

- il faut **analyser la carte de ressources** du dump ROM pour localiser la ressource — travail non trivial
  mais documenté (format des forks de ressources, *Inside Macintosh*) ;
- les sons Mac sont en **8 bits non signés, mono, 22 254,54 Hz** : rééchantillonnage vers la sortie HDMI ;
- l'audio doit donc être initialisé **très tôt**, avant l'émulateur — ce qui remonte la phase audio dans
  l'ordre logique, au moins pour un chemin minimal ;
- rien n'est embarqué dans le dépôt : le son vient de **la ROM de l'utilisateur**. Pas de carillon sans ROM ;
- une fois qu'il existe, le firmware le **rejoue une seconde fois** quand la PRAM est oubliée (§7.12) : c'est
  ainsi qu'un Macintosh accusait réception de Cmd-Option-P-R, et c'est le seul retour que l'utilisateur ait.

**Happy Mac et Sad Mac** : mêmes icônes, même origine, même méthode d'extraction. Le Sad Mac est en outre un
vrai système de diagnostic — l'icône était accompagnée d'un code hexadécimal. Okapia reprend le procédé
avec **ses propres codes**, documentés : ROM absente, ROM non reconnue, aucun disque amorçable, carte SD
illisible, mémoire insuffisante. C'est bien plus dans l'esprit de la machine qu'une trace sur la liaison
série — qui reste, elle, disponible pour le détail.

Nuance : lorsque Basilisk démarre normalement, **la ROM affiche elle-même son Happy Mac**. Le nôtre le
précède et couvre l'intervalle entre la mise sous tension et l'entrée dans l'émulation.

### 7.14 Provisionnement : disque persistant, catalogue en ligne, bibliothèque

Objectif : **pouvoir essayer Okapia sans rien préparer**. À la première mise sous tension, si la carte SD ne
contient aucune image, le firmware (§7.12) propose de provisionner.

**Trois briques, par ordre de valeur :**

1. **Disque persistant par défaut.** Une image vide, formatée HFS, créée sur la carte au premier démarrage —
   l'équivalent du « Saved HD » d'Infinite Mac. Les documents de l'utilisateur y survivent aux mises à jour
   et aux changements de système. À combiner avec le dossier partagé (§7.6), qui couvre les échanges avec le
   monde extérieur.
2. **Catalogue en ligne**, présenté en arborescence : `System 6 / System 7 / Mac OS 8 / Mac OS 9` (ce
   dernier le jour où un moteur PowerPC existe), puis les versions précises en feuilles. Sélection,
   téléchargement, écriture sur SD, démarrage. Techniquement : `CHTTPClient` de Circle, et **HTTPS via
   mbed TLS**, que circle-stdlib sait construire (`--opt-tls`) — indispensable, la plupart des sources ne
   servant plus qu'en HTTPS.
3. **Bibliothèque de logiciels** séparée du système, sur le modèle de l'« Infinite HD » : un catalogue de
   métadonnées, les fichiers étant téléchargés à la demande depuis leurs hébergeurs d'origine.

**Ce qu'il faut savoir avant de s'engager :**

- une image système pèse de quelques dizaines à quelques centaines de mégaoctets ; sur Pi 3 (réseau via USB)
  le téléchargement sera lent. Prévoir la **reprise après coupure** — d'où l'intérêt du découpage en blocs
  avec manifeste, exactement ce que fait Infinite Mac ;
- il faut **décompresser** : `zlib` est portable et suffit pour `.gz`/`.zip` ; les formats Mac (`.sit`,
  `.hqx`) ne sont pas à traiter côté firmware — c'est le rôle de StuffIt Expander **dans** le Mac, via le
  dossier partagé ;
- **ne rien réhéberger.** Le catalogue est une liste d'URL vers des sources tierces, son adresse est
  configurable, et la fonction est désactivable. C'est la position la plus défendable, et elle laisse à
  chacun la possibilité d'héberger son propre catalogue.

**À reprendre d'Infinite Mac** (Apache-2.0, `mihaip/infinite-mac`) — des idées, pas du code : le disque
persistant par défaut, le découpage en blocs avec manifeste pour un téléchargement repris, la séparation
système / bibliothèque de logiciels, les CD-ROM montables. À noter, ce projet utilise **le même fork macemu
que nous** (`kanjitalk755`), ce qui conforte le choix d'amont.

## 8. Ce qui est réutilisé, ce qui est à écrire

C'est l'application du principe du §3.1, et cela change l'échelle du projet.

**Réutilisé depuis l'amont** (circle-stdlib fournit `open`/`read`/`write`/`lseek`, `opendir`/`readdir` sur
FatFs, `stat`, `mkdir`, `rename`, `unlink`, les sockets BSD, `select`, `clock_gettime`) :

| Fichier amont | Statut |
|---|---|
| `BasiliskII/src/dummy/*` | stubs propres déjà écrits (audio, ether, scsi, serial, clip, xpram, prefs) |
| `CrossPlatform/video_blit.cpp` | conversions de pixels et endianness |
| `Unix/extfs_unix.cpp` (400 l.) | dossier partagé — presque tel quel |
| `Unix/prefs_unix.cpp` | configuration — inutile d'inventer un format `.ini` |
| `Unix/xpram_unix.cpp` | PRAM |
| `Unix/timer_unix.cpp` | horloge, en grande partie |
| `Unix/sys_unix.cpp` | accès aux images disque (les parties `/dev` sont à écarter) |
| `src/slirp/` | si l'on en vient là |

**À écrire — cinq fichiers, tous de couche matérielle :**

| Fichier | Contenu |
|---|---|
| `main_circle.cpp` | initialisation, allocation mémoire précoce, boucle, tick 60 Hz, répartition S1 |
| `video_circle.cpp` | table des modes, compositeur, palette, gamma |
| `input_circle.cpp` | USB HID → ADB |
| `ether_circle.cpp` | `SendFrame`/`ReceiveFrame` |
| `audio_circle.cpp` | HDMI, tampons, DMA |

Réserve honnête : les fichiers Unix ne compileront pas *tels quels* — chemins, `#ifdef` Linux, absence de
`/dev`, encodage des noms. On part de code éprouvé, pas d'une page blanche. Chaque fichier adapté porte en
en-tête sa provenance exacte et la liste des modifications.

---

## 9. Vérifications préalables

Trois essais courts, **avant tout code Basilisk**. Ils coûtent une journée et évitent des mois mal orientés.

**V1 — Framebuffer Pi 5.** Sur un Pi 5 réel : demander 640×480×8 à `CBcmFrameBuffer`, afficher ce que le
firmware donne réellement (largeur, hauteur, profondeur, pitch), tester `SetPalette`/`UpdatePalette`,
`SetVirtualOffset` et `WaitForVerticalSync`. La résolution sera ignorée, c'est documenté ; ce que l'essai
révèle, c'est si la profondeur 8 bits et la synchronisation verticale sont utilisables. **Décide du statut
du Pi 5.** Sans matériel Pi 5 sous la main, cet essai peut attendre — le plan ne dépend pas de lui.

**V2 — Le cœur compile-t-il ? — fait le 2026-08-26, concluant.**

Sondage de compilation seule (`tests/core-probe/`, cible `probe`) sur les 28 fichiers du cœur, avec la
toolchain Circle, en `DIRECT_ADDRESSING`, `FPU_UAE`, `EXCEPTIONS_VIA_LONGJMP` :

**24 sur 28 compilent tels quels**, dont `newcpu.cpp` (le cœur 68k), `memory.cpp`, `basilisk_glue.cpp`,
`fpu_uae.cpp`, `video_blit.cpp`, `emul_op.cpp`, `extfs.cpp`, `rom_patches` mis à part, et toute la couche
Macintosh (adb, audio, disk, scsi, sony, timer, video, xpram, slot_rom).

Les quatre exceptions, toutes qualifiées :

| Fichier | Cause | Coût |
|---|---|---|
| `bincue.cpp` | dépend de SDL (`SDL_mutex`) | **nul** : option amont `--with-bincue`, désactivée par défaut, on ne le compile pas |
| `rom_patches.cpp`, `rsrc_patches.cpp` | `htons`/`ntohs` non déclarés | **un include** : circle-newlib les fournit dans `<arpa/inet.h>` |
| `ether.cpp` | `<sys/ioctl.h>` absent de newlib | **mineur** : sert à `ioctl(FIONBIO)` sur une socket UDP, un chemin que `ether_circle.cpp` n'empruntera pas |

**Ce que ce sondage a établi qu'il faut écrire**, et qui n'était pas dans le plan :

- un **`config.h`** à la main : le cœur inclut `<config.h>`, normalement produit par autoconf, qui n'existe
  pas en bare-metal. Une trentaine de lignes déclarant les tailles de types et les en-têtes présents ;
- un **`sysdeps.h` Okapia**, comme chaque plateforme amont a le sien (`Unix/`, `Windows/`, `BeOS/`,
  `AmigaOS/`) : types de base, ordre des octets, en-têtes système ;
- compiler en **`-std=gnu++17`** et non `c++17` : ce dernier définit `__STRICT_ANSI__`, qui masque `strdup`
  et consorts dans newlib.

**V2bis — Les exceptions C++ fonctionnent-elles sous Circle ? — oui, mesuré.** Un jet rattrapé à travers
quatre niveaux d'appels, dans le test de la phase 1 (`tests/smoke/exctest.cpp`). C'est ce qui permet de
garder le chemin d'exception **par défaut de l'amont** plutôt que le chemin `setjmp` que personne
n'emprunte. Le test reste dans le smoke test : si un futur modèle ou une future version de Circle casse le
déroulement de pile, on le saura au démarrage et non au milieu du boot de Mac OS.

**Conséquence sur le risque R3** (« dépendances POSIX cachées ») : il est largement levé. Le cœur de
Basilisk II est bien plus portable que le plan initial ne le redoutait — l'essentiel du couplage à Unix
vit dans la couche plateforme, pas dans le cœur.

**V3 — Mémoire exécutable.** Écrire quelques instructions ARM64 dans un tampon, `dc civac` / `ic ivau` /
`isb`, sauter dedans. Cela échouera : Circle marque toutes les pages au-delà de `_etext` en **`PXN = 1`**
(`lib/translationtable64.cpp`), donc le tas n'est pas exécutable en EL1. La taille du patch nécessaire dit
tout de la faisabilité d'un JIT futur. **Sans objet tant que le JIT n'est pas au programme** — à faire
seulement si la question se pose.

---

## 10. Phases

### Phase 0a — Poste de travail

Avant tout code : rendre le travail **local, reproductible et économe**. Un agent qui doit interroger le
réseau pour lire une documentation ou un fichier amont coûte du temps et des tokens à chaque question.

**Consignes d'agent.** `AGENTS.md` est écrit en premier — budgets (tokens et ressources du Pi), chemins
locaux, invariants, pièges vérifiés, conventions. `CLAUDE.md` s'y ramène par un simple import, pour n'avoir
qu'une seule source de vérité.

**Outils, sur macOS.** Déjà présents sur le poste : `git`, `python3`, `rg`, `ccache`, `curl`, `xz`, `unzip`.
Restent à installer :

| Outil | Pourquoi |
|---|---|
| `qemu` (Homebrew) | `qemu-system-aarch64 -M raspi3b` |
| `make` 4.x (Homebrew) | le `make` d'Apple est en 3.81, trop ancien |
| toolchain **aarch64-none-elf** | **ARM GNU 15.2.Rel1**, celle que Circle et circle-stdlib testent — à prendre chez ARM, pas l'`aarch64-elf-gcc` 16.x de Homebrew, non validée en amont |
| `dtc` (optionnel) | inspection des arbres de périphériques Pi 5 |

`ccache` étant déjà là, l'activer dans la chaîne de compilation : les rebuilds du cœur UAE sont longs.

**Sources locales.** Le bootstrap clone tout ce qui sert à lire, pas seulement ce qui sert à compiler —
c'est ce qui permet de travailler sans réseau :

```sh
# dépendances de build (sous-modules, épinglés)
external/circle-stdlib/          # contient Circle dans libs/circle/
external/macemu/

# références, ignorées par Git, clonées en --depth 1
reference/bmc64/                 # émulateur complet sur Circle
reference/M5Tab-Macintosh/       # Basilisk embarqué (lecture seule : licence non déclarée)
reference/macemu-jit/            # JIT AArch64 + corpus de tests
reference/amiberry/              # amont du backend JIT ARM
reference/infinite-mac/          # provisionnement — sans les images (elles pèsent des centaines de Mo)
```

Ordre de grandeur : moins de 500 Mo au total en `--depth 1`, pour 133 Go disponibles. La documentation de
Circle vient avec (`libs/circle/doc/`) : plus aucune raison d'aller la chercher en ligne.

**Succès** : `rg` trouve n'importe quelle réponse sur Circle, macemu ou les références sans accès réseau.

### Phase 0b — Bootstrap

- [ ] `LICENSE` = **GPLv3**, README qui l'énonce
- [ ] `.gitignore` (dont `roms/`, `reference/`)
- [ ] sous-modules `external/circle-stdlib` et `external/macemu`, épinglés, SHA documentés
- [ ] `scripts/bootstrap.sh` : toolchain, dépendances, configuration de circle-stdlib
- [ ] `scripts/fetch-references.sh`
- [ ] documentation de l'environnement de compilation **sur macOS**

Environnement macOS : ni QEMU ni cross-compilateur ne sont présents par défaut, et `make` est la version
3.81 d'Apple. Il faut `qemu`, `make` 4.x, et une toolchain **aarch64-none-elf** — Circle et circle-stdlib
recommandent la **ARM GNU 15.2.Rel1**, à préférer aux versions plus récentes de Homebrew.

Le `configure` de circle-stdlib couvre déjà `--qemu`, `-r <1-5>` et `--kernel-max-size` (4 Mo par défaut,
au lieu des 2 Mo de Circle seul — ce qui évite une erreur d'édition de liens obscure).

**Succès** : `./scripts/bootstrap.sh` prépare le dépôt sur une machine neuve, outils compris.

### Phase 1 — Circle sous QEMU

Cible : `raspi3b`, AArch64.

- [x] démarrage, journal série
- [x] framebuffer : afficher résolution, profondeur, pitch, adresse, une mire, un compteur d'images
- [x] palette 8 bits (QEMU la gère : son modèle `bcm2835_fb` traite 8, 16 et 32 bpp avec table)
- [x] minuterie
- [x] clavier USB (`-device usb-kbd`)
- [x] souris USB (`-device usb-mouse` ; la documentation Circle note que le curseur ne fonctionne pas —
      ne pas en faire un critère bloquant)
- [x] carte SD (`-drive if=sd`), lecture d'un fichier via FatFs
- [x] débogage GDB (`-s -S`)

**Succès** : `Circle boot OK / Framebuffer OK / Keyboard OK / SD OK / GDB OK`.

**Fait le 2026-08-26.** Relevé réel sous QEMU `raspi3b`, Circle 51, GCC 15.2.1 :

```
Frame buffer requested 640x480x8
Frame buffer granted   640x480, 8 bpp, pitch 640, 300 KB at 0x3C100000
Palette test ok (256 entries)
okapia.txt: Okapia smoke test: this line was read from the SD card.
Keyboard attached / Mouse attached
```

Le pitch vaut exactement largeur × bpp / 8 : pas de remplissage de ligne sous QEMU. Le port GDB 1234
répond. Le mode indexé 8 bits et sa palette fonctionnent **sous QEMU** — reste à vérifier sur matériel,
et surtout sur Pi 5 (§9 V1), où c'est précisément ce qui est en doute.

**Deux enseignements repris dans `AGENTS.md`** : la série doit être initialisée avant tout le reste, sinon
un échec précoce ne se distingue pas d'un blocage ; et `DEPTH` étant compilé dans `libcircle.a`, un mode
indexé exige notre propre `CBcmFrameBuffer` plutôt que `CScreenDevice`.

**La couche `stdio` est validée**, ce qui conditionnait la réutilisation d'`extfs_unix.cpp` et de
la lecture des préférences depuis la carte (§8) :

```
stdio fopen/fgets: Okapia smoke test: this line was read from the SD card.
stdio opendir/readdir: 1 entries
```

Le blocage initialement observé venait d'un `CConsole (0, &m_Serial)` : ce constructeur exige **deux**
périphériques non nuls (`assert (m_pInputDevice != 0)`, `lib/input/console.cpp:48`). Une assertion qui
échoue dans un **constructeur de membre** s'exécute avant l'initialisation série : sortie muette,
indiscernable d'un blocage au démarrage. La bonne forme est `CConsole (&m_Serial, &m_Serial)` — console
sur la série, ce qui est de toute façon la configuration d'Okapia, l'écran appartenant au Mac.

Note de chemin : sous `stdio`, la carte se lit en `/okapia.txt`, pas `SD:/okapia.txt` — `CGlueStdioInit`
monte la partition par défaut sur la racine. Les appels FatFs directs, eux, gardent le préfixe `SD:`.

### Phase 2 — Le même binaire sur Pi 3 ou Pi 4

Ne pas repousser le matériel à la fin. Reprendre la phase 1 sur carte réelle, dans la même semaine.

**Bloquée : pas de carte en main.** Rien n'est vérifié sur matériel réel — tout ce qui suit n'est
établi que sous QEMU, et `raspi3b` ment (§Pitfalls d'`AGENTS.md`).

- [ ] `RASPPI=3` puis `4`, journal série (UART ou Debug Probe)
- [ ] HDMI réel, SD réelle, USB réel
- [ ] cycle édition → compilation → flash → redémarrage → journal, sans manipuler la carte SD
      (bootloader série, cFlashy ou TFTP selon ce que Circle permet)

### Phase 3 — Le cœur Basilisk compile et démarre

- [x] `uae_cpu_2021` + `src/*.cpp` compilés avec circle-stdlib
- [x] `src/dummy/*` en place pour tout ce qui n'est pas encore écrit
- [x] configuration lue depuis la SD — fait le 2026-08-29, mais par `src/circle/prefs_circle.cpp`
      plutôt que par `prefs_unix.cpp` : ce dernier construit son chemin à partir de `$HOME` et de
      `$XDG_CONFIG_HOME`, ce qui n'a pas de sens ici. Le **format** et l'analyseur restent ceux de
      Basilisk, seul le chemin est à nous. Détail en §15bis, brique 1
- [x] `main_circle.cpp` : allocation du bloc 257 Mo **en premier**, `MEMBaseDiff`, tick 60 Hz par IRQ

Règle : aucun stub silencieux. Tout stub journalise son appel et échoue proprement s'il est indispensable.

**Succès** : le binaire contient le cœur et atteint l'initialisation du moteur 68k.

**Générateur d'opcodes — fait le 2026-08-26.** `scripts/gen-cpu.sh` monte la chaîne à deux étages :
`table68k` → `build68k` (compilé **pour macOS**) → `cpudefs.cpp` → `gencpu` (idem) → `cpuemu.cpp`,
`cpustbl.cpp`, `cpufunctbl.cpp`, `cputbl.h`. Environ **195 000 lignes générées**, toutes compilées pour
AArch64 sans une seule erreur. `cpuemu.cpp` se compile en huit unités (`-DPART_1` à `-DPART_8`), comme en
amont.

**Budget de taille mesuré**, et il est rassurant :

| | text | data |
|---|---|---|
| `cpuemu1-8` (l'interpréteur) | ~310 Ko | — |
| `cpufunctbl` (table de dispatch) | — | **512 Ko** (65 536 pointeurs) |
| `cpudefs` + `cpustbl` | ~34 Ko | ~25 Ko |
| **Cœur 68k complet** | **~861 Ko** | |

À comparer aux 497 Ko du noyau de la phase 1 (Circle + newlib + notre test) et aux **4 Mo** de
`KERNEL_MAX_SIZE`. La marge est confortable : le risque « dépassement de taille du noyau » du §7.2quater
est levé pour l'interpréteur. Il faudra le réévaluer si un JIT arrive un jour.

Note : les fichiers `.o` font plusieurs fois cette taille (`cpufunctbl.o` pèse 2 Mo) parce que Circle
compile avec `-g`. Ce sont les sections `text`/`data` qui comptent, pas la taille du fichier objet.

**Première édition de liens complète — faite le 2026-08-26.** `tests/link-probe/` compile **le cœur
entier** — 21 fichiers Macintosh, le CPU `uae_cpu_2021`, le FPU `fpu_uae`, `video_blit`, les tables
générées (dont `cpuemu` en huit parties), les neuf bouchons `dummy/` et nos fichiers de compatibilité :
**50 unités, aucune erreur.**

Le lien produit **163 symboles indéfinis**, dont **87 sont fournis par les bibliothèques** (libc, libm,
libstdc++, Circle) et seront résolus au lien final. **Il reste donc 76 fonctions à écrire**, et elles se
répartissent d'elles-mêmes en cinq fichiers :

| Futur fichier | Symboles | Nature |
|---|---|---|
| `sys_circle.cpp` | 25 | accès aux images disque, CD-ROM, disquettes — `sys_unix.cpp` en couvre une partie (§8) |
| `main_circle.cpp` | 15 | verrous `B2_*_mutex`, `ErrorAlert`/`WarningAlert`/`QuitEmulator`, drapeaux d'interruption, variables globales `CPUType`/`FPUType`/`ScratchMem` |
| `extfs_circle.cpp` | 14 | dossier partagé — `extfs_unix.cpp` est réutilisable presque tel quel (§7.6) |
| `timer_circle.cpp` | 8 | `TimerDateTime`, `Microseconds`, ticks et repos du CPU |
| `video_circle.cpp` | 4 | `VideoInit`, `VideoExit`, `VideoInterrupt`, `VideoQuitFullScreen` |

Plus une poignée d'isolés : `FlushCodeCache` (vidage du cache d'instructions), `access`, `creat`,
`gethostname` (absents de newlib), et `cpu_do_check_ticks` / `emulated_ticks` / `tick_inhibit` /
`idle_wait` / `idle_resume`, qui relèvent du cadencement.

La liste vit dans `tests/link-probe/platform-todo.txt` et sert d'indicateur : elle doit rétrécir à chaque
fichier écrit, et atteindre zéro marque la fin de la phase 3.

**Avancement** : 76 → **36**.

| Étape | Restant | Comment |
|---|---|---|
| `main_circle.cpp` | 76 → 58 | écrit : mémoire Mac, ROM, interruptions, verrous, alertes |
| `timer_circle.cpp` | 58 → 50 | écrit sur `CTimer` |
| `extfs_unix.cpp` **réutilisé** | 50 → 36 | **aucune logique écrite** : le fichier amont compile tel quel, il ne manquait qu'un `utime` de quinze lignes |
| `sys_unix.cpp` **réutilisé** | 36 → 13 | **zéro modification, zéro shim** : tout l'accès aux images disque compile en l'état |
| `video_circle.cpp` | 13 → 9 | écrit : sortie fixe, table des modes, compositeur, palette |
| compat + cadencement | 9 → **1** | `access`, `creat`, `sleep`, `gethostname`, les compteurs de ticks, le facteur *sparsebundle* |

**Terminé.** Le seul symbole restant est `__cxa_pure_virtual`, **faible**, fourni par libsupc++ au lien
final. La couche plateforme compile et se lie contre le cœur complet : 59 unités.

Le pari du §8 est donc vérifié sur son premier cas réel : le dossier partagé, avec ses forks de ressources
et sa table de types, a coûté un shim au lieu de 400 lignes.

**Il reste treize symboles**, et un seul représente un vrai travail :

- **`video_circle.cpp`** (4) : `VideoInit`, `VideoExit`, `VideoInterrupt`, `VideoQuitFullScreen`. Quatre
  points d'entrée, mais tout le compositeur du §7.2 derrière ;
- **compat libc** (4) : `access`, `creat`, `sleep`, `gethostname`, absents de newlib ;
- **cadencement** (3) : `cpu_do_check_ticks`, `emulated_ticks`, `tick_inhibit` ;
- **`disk_sparsebundle_factory`** (1) : format d'image propre à macOS, à bouchonner ;
- `__cxa_pure_virtual` (faible, runtime C++).

**Ce que ces chiffres disent du plan.** Les deux fichiers les plus volumineux de la couche plateforme —
39 symboles à eux deux, l'accès disque et le partage de fichiers — ont été obtenus en réutilisant l'amont
sans écrire de logique. Le §8 tablait là-dessus sans pouvoir le démontrer ; c'est démontré.

Trois frictions de frontière rencontrées à l'écriture, valables pour tout fichier plateforme :

- **`ASSERT_STATIC` manque** dès qu'on inclut un en-tête Circle : `circle/types.h` l'attend de l'`assert.h`
  *de Circle*, mais c'est celui de newlib qui gagne la recherche. `src/circle/okapia_circle.h` définit la
  macro puis inclut Circle — à inclure avant tout `<circle/...>` ;
- **`circle/new.h` est inutilisable avec la STL** : son `operator new (size_t, void*)` a une spécification
  d'exception différente de celle de `<new>`. Pour un bloc brut, appeler
  `CMemorySystem::HeapAllocate (size, HEAP_ANY)` directement ;
- **les globales mémoire appartiennent à `basilisk_glue.cpp`** (`RAMBaseHost`, `RAMSize`, `ROMBaseHost`,
  `ROMSize`, `RAMBaseMac`, `ROMBaseMac`, `MEMBaseDiff`). La couche plateforme les **remplit**, elle ne les
  définit pas — sous peine de doublon à l'édition de liens.

**Deux ajustements qu'il a fallu faire**, tous deux dans `src/circle/compat/` plutôt que dans l'amont :
`<sys/ioctl.h>`, absent de newlib, et `gethostbyname`/`struct hostent`, que circle-newlib ne fournit pas
(il n'a que `getaddrinfo`). Les deux ne servent qu'au tunnel UDP d'`ether.cpp`, un chemin qu'Okapia
n'emprunte pas mais qui est choisi **à l'exécution** et doit donc compiler. Les bouchons signalent leur
appel plutôt que de mentir.

**Piège rencontré, à ne pas refaire** : ne jamais mettre un `#include` de header système dans `config.h`.
Ce fichier est tiré avant tout le reste, et y faire entrer `<arpa/inet.h>` a cassé l'ordre d'inclusion de
**tout** le cœur (`locale_t` non défini, 39 fichiers en échec). Un `config.h` déclare, il n'inclut pas.

### Phase 4 — ROM et mémoire Mac

- [x] chargement de `Q650.ROM` depuis la SD, taille vérifiée et image refusée si elle n'est pas
      32-bit clean (mot de version, `rom_patches.cpp:838`) — ce n'est pas une somme de contrôle
- [x] application des ROM patches
- [ ] initialisation du 68040 faite, mais **PC et SP initiaux ne sont pas affichés** — on journalise
      le type de CPU, la FPU et le mode d'adressage, pas l'état de départ

**Succès** : `ROM loaded / ROM patches applied / Mac RAM allocated / 68040 initialized / Entering 68k`.

**Atteint le 2026-08-26**, sous QEMU `raspi3b`, avec la ROM `F1ACAD13` :

```
okapia: Free before Mac RAM: 935 MB low, 935 MB high
okapia: Mac memory: 256 MB RAM at 0x807e80, ROM at 0x10807e80, Mac base diff 0x807E80
okapia: ROM: /okapia.rom, 1024 KB, 32-bit clean
okapia-video: Output: 640x480, 32 bpp, pitch 2560
okapia-video: 12 modes offered, 1200 KB guest buffer
okapia-video: Mac mode 640x480 8 bpp, shown at 1x scale, origin 0,0
okapia: Mac RAM at 0x807e80 (Mac 0x00000000), ROM at 0x10807e80 (Mac 0x10000000)
okapia: CPU type 4, FPU 1, 24-bit addressing off
okapia: Entering 68k execution
```

`kernel8.img` pèse **1,76 Mo** — cœur 68k, Circle, newlib et couche plateforme compris, contre 4 Mo de
`KERNEL_MAX_SIZE`. L'allocation des 256 Mo intervient bien avant les pilotes : 935 Mo étaient libres, et
l'ordre d'initialisation tient.

**Ce qui manque pour aller plus loin**, et qui est le sujet de la phase 5 : le **tick 60 Hz n'est pas encore
branché**. Sans interruption périodique, la ROM s'exécute mais n'avance pas — elle attend un temps qui ne
passe jamais. C'est le prochain travail, et il conditionne tout le reste du démarrage.

### État au 2026-08-26 : le Mac atteint le Happy Mac

Sous QEMU `raspi3b`, avec la ROM `F1ACAD13` et une image Mac OS 7.6 de 500 Mo :

```
RESET → Patch BootGlobs → Fix MemSize → SonyOpen → InstallDrivers
DiskOpen : disk inserted, 1024000 blocks, adding drive 1 → InstallSERD
écran gris → Happy Mac
```

Le Happy Mac n'apparaît qu'une fois les *boot blocks* lus et un dossier Système reconnu : le volume est donc
bien monté et lu. Puis **l'exécution se fige** : seul `EmulOp 7129` (`M68K_EMUL_OP_IRQ`) tourne encore, ce
qui veut dire que le Mac ne fait plus que traiter ses interruptions — il attend.

**Écarté par la mesure** : le Time Manager fonctionne (une seule tâche programmée, `PrimeTime time=300000`,
soit cinq minutes — il attend, il n'est pas cassé) ; l'activité clavier et souris ne débloque rien ; le
temps non plus (trois minutes ne changent rien).

**Piège de méthode découvert, et coûteux** : `-display none` **change le comportement du guest**. Toutes
les mesures faites en mode sans affichage montraient la disquette clignotante, là où une fenêtre ouverte
donne le Happy Mac. Reproduire avec fenêtre avant toute conclusion sur l'avancement du démarrage.

**Deux mesures qui n'en étaient pas**, à ne pas refaire : `DiskPrime` **n'émet aucune trace** dans
`disk.cpp`, donc en compter les occurrences ne prouve rien ; et `info blockstats` de QEMU rapporte zéro
lecture même quand la carte est manifestement lue, le contrôleur SD ne passant pas par la couche bloc.

### Phase 5 — Exécution 68k

- [x] exécution de la ROM, exceptions, accès mémoire, endianness
- [x] instrumentation des traps, traces des erreurs de bus et d'adresse : `exception_trace.cpp`,
      accroché par `--wrap` sans patcher `external/`, activé par `make OKAPIA_TRACE=1`
- [ ] niveaux de journalisation : `ERROR WARN INFO DEBUG TRACE_68K TRACE_IO` — **non fait**, on n'a
      que les niveaux de `CLogger` et deux interrupteurs de compilation

**Succès** : la ROM avance jusqu'à réclamer ses périphériques.

### Phase 6 — Disque

- [x] image brute sur SD, lecture et écriture : le Finder démarre, et une coupure par `SIGKILL`
      laisse le volume structurellement sain (`scripts/run-test.sh`)
- [ ] **erreurs et mode lecture seule jamais exercés** : carte pleine, échec d'écriture remonté par
      la couche SD, image en lecture seule, retrait en cours d'écriture (cf. §Data safety d'`AGENTS.md`)
- [x] journalisation des opérations : `trace_disk_circle.cpp`, bloc par bloc avec détection des
      lectures courtes, sous `make OKAPIA_TRACE=1`

**Succès** : atteint — la ROM détecte un disque amorçable et le Finder s'affiche.

### Phase 7 — Vidéo

- [x] framebuffer de sortie fixe, initialisé une fois
- [x] table des modes : les six profondeurs sont offertes (1, 2, 4, 8, 16, 32 bits), en 640×480 pour
      toutes et jusqu'à 1024×768 pour les modes indexés. Le défaut d'affichage du 1 bit venait de
      l'unité passée aux blitters — ils comptent des **octets source**, pas des pixels, ce qui ne se
      voit qu'en dessous de 8 bits. Les modes directs ont leurs propres convertisseurs, la table
      d'amont étant indexée sur la profondeur de sortie. Vérifié au Speedometer : les six tests de
      couleur donnent des valeurs (B&W 43,73 · 2 bits 41,79 · 4 bits 44,97 · 8 bits 48,39 ·
      16 bits 57,31)
- [ ] **anomalie non expliquée** : à mode actif identique (640×480 8 bits), une composition coûte
      270 µs quand 6 modes sont offerts et 1224 µs quand il y en a 22. Ni le tas, ni la taille du
      tampon (plafonner ne change rien), ni le mode réellement choisi. Trois hypothèses testées,
      trois fausses
- [x] compositeur : `Screen_blitter_init()`, `ExpandMap[]`, doublement entier, centrage
- [x] `switch_to_current_mode()`, `set_palette()`, `set_gamma()`
- [x] résolution de sortie détectée par `CBcmFrameBuffer(0, 0, 32)` auprès du firmware, puis table des
      modes Macintosh filtrée selon les dimensions réellement obtenues ; aucun réglage utilisateur
- [x] compteur d'images et coût de composition mesurés. Relevé sous QEMU `raspi3b`, 640×480 8 bits :
      composition 1530 µs, **8,5 % du temps mural** à 55 images/s, invité à 20 542 k opcodes/s
- [x] cadence pilotée par la préférence `frameskip` d'amont, plus par une constante. Le défaut d'amont
      (6, soit 10 Hz) était codé en dur : l'écran ne se rafraîchissait que 9 fois par seconde et, le
      curseur étant dessiné par le Mac dans son propre framebuffer, cela se lisait comme une souris qui
      traîne. Défaut d'Okapia : 1

**Succès** : Happy Mac, puis les premiers éléments graphiques du démarrage.

### Phase 8 — Clavier et souris

- [x] correspondance des codes, modificateurs, appui/relâchement
- [x] mouvement et boutons : `RegisterStatusHandler()` pour des déplacements bruts `dx/dy`. L'API
      « cooked » jetait chaque rapport faute de `Setup()`, et livre des coordonnées absolues
      qu'il ne faut pas passer en relatif
- [x] table clavier **générée** depuis `BasiliskII/src/Unix/keycodes` (section `sdl cocoa`) par
      `scripts/gen-keycodes.py`, et non plus écrite à la main : les scancodes SDL2 **sont** les usages
      USB HID, donc cette section est déjà la table qu'il faut à un hôte USB. Fait le 2026-08-29 après
      qu'une table manuelle eut envoyé les quatre flèches en *virtual key codes* au lieu de codes ADB
      bruts — Haut partait en 0x7E, tombait à côté de la touche Power et ouvrait le dialogue d'extinction
      à chaque appui. La table générée apporte aussi le pavé numérique, absent de la version manuelle
- [x] écrasement depuis la carte par `keycodefile` (mot-clé d'upstream) : un `BasiliskII.keycodes` copié
      d'un Basilisk de bureau se charge tel quel — 105 correspondances, vérifié. Deux lignes suffisent à
      corriger deux touches, sans recompiler
- [ ] raccourcis de débogage hors Mac — non faits

**Succès** : Mac OS utilisable.

### Jalon majeur A — Finder utilisable

Sous QEMU **et** sur Pi 3/4 : ROM chargée, disque amorçable, Finder affiché, clavier et souris
fonctionnels, système stable plusieurs minutes, redémarrage reproductible, journaux propres.

**Atteint sous QEMU, pas sur matériel** (phase 2 bloquée). Vérifié le 2026-08-26 : ROM chargée, disque
amorçable, Finder affiché, clavier et souris fonctionnels, extinction et redémarrage propres depuis le
menu Spécial, journaux propres (58 lignes pour 30 s, contre 2,8 millions avant la reconstruction). Reste
en défaut : la fluidité — la souris traîne et la vidéo semble coûteuse, ce qui n'a pas encore été mesuré
(phase 11).

### Phase 9 — Dossier partagé

Fait le 2026-08-29. La surprise est qu'il n'y avait presque rien à porter : `extfs_unix.cpp` compile tel
quel contre newlib, et `extfs.cpp` n'utilise que `stat`, `access`, `opendir`/`readdir`, `open`/`read`/
`write`/`lseek`, `mkdir`, `remove`, `rmdir`, `rename` et `utime` — tous présents, sauf le dernier.

**Le vrai obstacle n'était pas FatFs, c'était MacOS.** ExtFS repose sur l'interface File System Manager
1.2, **intégrée à Mac OS 7.6 et suivants, et disponible en extension système pour les versions
antérieures** (`BasiliskII/TECH` §6.10). Sans elle, `extfs.cpp:491` écrit « No FSM present, disabling
ExtFS » et il n'y a pas de volume partagé — ce que donne un System 7.1 nu, et c'est ce qui a masqué le
résultat au premier essai, la carte démarrant alors sur le 7.1.

À ne pas confondre avec le **partage de fichiers** : celui-là est AppleShare sur le réseau, il expose les
dossiers du Mac à *d'autres Macs*, et n'apporte rien ici. Le test d'`extfs.cpp:487` étant un appel
**Gestalt** (`gestaltHasFileSystemManager`, puis version ≥ 1.2), une extension le satisfait aussi bien
qu'une intégration au Système.

**Vérifié le 2026-08-29 sur System 7.1.2 français**, avec le System Enabler 040 et le File System Manager
1.2 pris dans le SDK Apple (<https://www.macintoshrepository.org/2070-file-system-manager-1-2-sdk>) : le
volume partagé se monte, l'avertissement « No FSM present » disparaît, et la détection annonce
« System 7.1.2 (F1-7.1.2) — model 5 ». Donc **la limite n'est pas la version du Système mais la présence
de l'extension** — c'est un problème d'installation, pas une impossibilité, et l'inverse aurait fait
renoncer à tort sur tout ce qui précède 7.6.

Sur la borne haute, deux sources qui ne s'accordent pas tout à fait : le `TECH` d'upstream écrit « built
into MacOS 7.6 (and later) », l'usage courant place l'intégration dès 7.5. Mesuré ici : 7.6.1 fonctionne
sans rien installer, 7.1.2 demande l'extension. Le README annonce 7.5 ; entre les deux rien n'est testé, et
c'est sans conséquence puisque le symptôme est explicite dans le journal.

**Un seul dossier, par construction.** `extfs.cpp` tient un unique `RootPath`, un unique `ROOT_ID` et un
unique enregistrement de volume : plusieurs dossiers partagés demanderaient de réécrire le fichier, qui
est dans `external/`. Des sous-dossiers dans le dossier partagé font le même service.

- [x] `extfs_unix.cpp` réutilisé sans modification ; seul `utime()` manquait et passe maintenant par
      `f_utime` (`FF_USE_CHMOD` est à 1 dans le `ffconf.h` de Circle)
- [x] `.finf/` et `.rsrc/` créés à la demande, table des extensions fonctionnelle : un `.txt` déposé
      depuis le Mac hôte arrive avec l'icône d'un document texte
- [x] essai de bout en bout : fichier déposé dans `qemu/sd-contents/shared/` → visible dans le Finder avec
      la bonne icône et la bonne place libre → le Finder y crée un dossier → le dossier et les deux
      répertoires auxiliaires apparaissent sur la carte. Refait sur le 7.1.2 muni du FSM
- [x] nom du volume paramétrable (`extfsname`) — en amont c'est une chaîne par plateforme, pas une
      préférence : chaque portage réécrit `STR_EXTFS_VOLUME_NAME` dans sa propre table
- [x] politique de vidage (`extfs_sync_circle.cpp`) : FatFs garde la queue d'une écriture en mémoire et
      n'inscrit la nouvelle taille qu'à la fermeture. L'image disque n'a pas cette fenêtre — le Mac écrit
      des secteurs entiers de 512 octets, que FatFs transmet directement — mais le dossier partagé écrit
      des longueurs quelconques. Sans vidage, une coupure laisse une entrée de répertoire à zéro octet
      pour un fichier dont les données sont déjà sur la carte : une perte qui ressemble à un succès

**Retombée** : l'horloge de Circle est maintenant posée à l'heure de compilation au démarrage
(`kernel.cpp`). Sans cela `get_fattime()` renvoie zéro et chaque fichier créé par le Mac porte la date
`1980-00-00`, qui n'est même pas une date valide. Le journal série est daté par la même occasion.

**Outillage** : le moniteur QEMU pilote la souris et le clavier du Mac (`mouse_move`, `mouse_button`,
`sendkey`), ce qui permet de vérifier le Finder sans personne devant l'écran. Deux pièges : les commandes
envoyées en rafale sur une seule connexion sont perdues — une par connexion, avec une pause — et deux
`mouse_button` ne font pas un double-clic, l'aller-retour du moniteur étant plus lent que le délai du Mac.
`Command-O` ouvre la sélection et remplace le double-clic.

### Phase 10 — Horloge, PRAM, arrêt propre

**État de l'horloge au 2026-08-29** — une seule chaîne, un seul point de décision :

1. **La source** : `OKAPIA_BUILD_TIME`, posé par le Makefile (`date +%s`, donc un instant UTC) et figé
   dans le noyau. C'est tout ce qu'il y a : un Pi n'a pas de RTC et NTP demande le réseau.
2. **Le point de décision** : `CKernel::Initialize()` appelle `m_Timer.SetTime()` une fois, puis
   `ApplyTimeZone()` après lecture des préférences. C'est là que viendront NTP, un RTC et la dernière
   heure connue de la carte — rien d'autre n'aura à changer.
3. **Les lecteurs** : le Mac par `TimerDateTime()` (`timer_circle.cpp`), FatFs par `get_fattime()`
   (`ffsystem.cpp`). Tous deux lisent l'horloge de Circle, donc la barre de menus du Mac et les dates sur
   la carte ne peuvent pas diverger.

**Deux pièges levés en route.** Le premier : `TimerDateTime()` portait son propre repli sur
`OKAPIA_BUILD_TIME`, hérité de l'époque où l'horloge de Circle restait à zéro. Deux mécanismes qui se
recouvraient, et de quoi ne plus savoir lequel décidait — supprimé, la fonction ne fait plus que lire.

Le second : **le Mac de cette époque n'a pas de fuseau horaire du tout**, son horloge *est* l'heure locale,
alors que Circle rapporte UTC. Un Pi à Paris affichait donc deux heures de retard, ce qui ressemble à un
noyau périmé et n'en est pas. D'où la préférence `timezone`, en minutes à l'est d'UTC. Vérifié : à `120`,
la barre de menus du Mac affiche 14:28 quand l'hôte affiche 14:28.

L'ordre compte : `SetTimeZone()` avant `SetTime()`, parce que `SetTime` range des secondes locales — poser
le fuseau après déplacerait l'heure du journal sans toucher celle du Mac.

**L'écriture de l'horloge par le Mac est perdue**, et c'est le comportement d'upstream sur toutes les
plateformes : `emul_op.cpp:181` ignore les écritures dans les registres RTC, et la lecture suivante rend
l'heure de l'hôte. Régler la date dans le tableau de bord ne tient pas.

#### La chaîne de confiance qui reste à construire

Aujourd'hui il n'y a **qu'une source, l'heure de compilation, et rien n'est persisté** : à chaque
démarrage l'horloge repart de l'instant où `make` a tourné. Le Mac ne peut pas la corriger non plus
(`emul_op.cpp:181` jette les écritures RTC). Ordre visé, du plus fiable au moins :

**Brique 1 — le RTC d'abord.** Circle fournit déjà tout, dans `addon/rtc/` :

- `CRealTimeClock` (`rtc.h`) est l'interface : `Get(CTime *)` et `Set(const CTime &)`, en UTC.
- `CFirmwareRTC` (`firmwarertc.cpp`) est le **RTC intégré du Pi 5**, via les property tags du firmware.
  Son `Initialize()` cherche `/soc/rpi_rtc` dans le device tree et rend `FALSE` ailleurs : il se détecte
  seul, on n'a pas à savoir sur quelle carte on tourne. Il ne garde l'heure hors tension qu'avec une pile
  sur le connecteur J5.
- `CMCP7941X` (`mcp7941x.cpp`) couvre les modules I²C (`CMCP7941X(CI2CMaster *, 100000, 0x6F)`), donc les
  Pi 3 et 4 avec un HAT à quelques euros.
- **`Set()` existe des deux côtés**, et c'est ce qui rend le RTC utile plutôt que décoratif : dès qu'une
  meilleure source apparaît (NTP), on la réécrit dedans.
- Construction : `addon/rtc` a son propre Makefile et produit `librtc.a`. Même traitement que
  `lib/sound/libsound.a`, déjà ajouté à `LIBS` — circle-stdlib ne les met pas dans la ligne de lien.
- **Bloqué par la phase 2**, et sans échappatoire : les machines de QEMU s'arrêtent à `raspi4b`, toutes
  en BCM283x/2711, aucune n'ayant d'horloge temps réel, et il n'existe pas de machine `raspi5` — donc pas
  de RTC du Pi 5 à simuler non plus. Le chemin doit donc se rabattre sur la
  brique 2 et le dire dans le journal, jamais échouer.

**Brique 2 — à défaut, la dernière heure connue du volume. Faite le 2026-08-29**
(`CKernel::RefineClock()`), la seule des trois qui marchait sans matériel ni réseau :

- [x] La source est `drLsMod` du MDB, que `hfs_vstat` rend déjà sous le nom `mddate` : seul le champ
      manquait dans `THfsVolumeInfo`, l'inventaire lisant déjà chaque image en lecture seule.
- [x] **Vérifié** : `d_ltime` (`data.c:467`) n'est ici qu'un décalage de 2082844800, parce que
      `HAVE_MKTIME` n'est jamais défini dans notre compilation et que `tzdiff` reste donc à 0
      (`data.c:456`). Pas de surprise de fuseau à l'intérieur de libhfs.
- [x] Règle : **retenue = max(le `mddate` le plus récent de la carte, `OKAPIA_BUILD_TIME`)**. On ne
      recule jamais, et une carte qui a servi hier vaut mieux qu'un noyau compilé le mois dernier. C'est
      un plancher, pas une correction.
- [x] **Piège de fuseau, évité** : un Mac écrit `drLsMod` en heure **locale**, donc `mddate` est une
      heure locale ramenée à l'époque 1970, tandis que `OKAPIA_BUILD_TIME` est un instant **UTC**. Les
      comparer directement décale d'une ou deux heures. On ramène le second au repère local avant le
      `max`, puis on pose le résultat avec `SetTime(t, TRUE)`.
- [x] **Garde-fou** : `drLsMod` tient sur 32 bits depuis 1904 et déborde en février 2040. Une date
      au-delà est un champ abîmé, pas une prophétie : elle est ignorée et journalisée.
- [x] **Place dans la séquence** : inventaire → **horloge** → dossier partagé → réparation → `InitAll`.
      L'affinage doit précéder toute écriture, sinon la réparation porte la mauvaise date — précisément
      ce que la brique corrige. L'inventaire tourne donc désormais toujours, `hfsinventory` ne
      commandant plus que son affichage.
- [x] **Vérifié** en compilant avec `OKAPIA_BUILD_TIME=1735689600` (1ᵉʳ janvier 2025) : l'horloge part de
      `Jan 1 02:00`, puis `Clock: Aug 29 12:17:55, from /boot71.img — later than the build time`. La
      source est nommée dans le journal, jamais devinée.

**Brique 3 — NTP, quand le réseau existera.** Envisageable, oui, et Circle l'a déjà :

- `CNTPClient::GetTime(CIPAddress &)` rend les secondes depuis 1970 en UTC, 0 en cas d'échec
  (`include/circle/net/ntpclient.h`) ; `CNTPDaemon(serveur, CNetSubSystem *)` resynchronise ensuite. Le
  modèle est l'exemple `18-ntptime` : `SetTimeZone()` puis `new CNTPDaemon("pool.ntp.org", &m_Net)`.
- **Le bon usage n'est pas d'afficher l'heure, c'est d'écrire le RTC** : une synchronisation, `Set()`
  dans le RTC, et les démarrages suivants n'ont plus besoin du réseau. NTP devient alors un confort, pas
  une dépendance — ce qui est la seule forme acceptable pour un appareil.
- Le démon corrige l'horloge pendant que le Mac tourne ; le Mac ne relit son RTC que de loin en loin,
  donc un saut finira par se voir. À surveiller plutôt qu'à craindre.

#### Ce que l'horloge demande au réseau, et ce qu'elle ne doit pas lui demander

C'est le point où la phase 10 touche la phase 13, et il vaut d'être posé maintenant pour ne pas se
retrouver à décider dans l'urgence :

- **NTP a besoin de la pile montée avant `Start680x0()`**, alors que la phase 13 ne prévoit le réseau que
  *pour le Mac*, c'est-à-dire pendant qu'il tourne. Ce sont deux besoins différents du même sous-système :
  l'un veut quelques secondes avant le démarrage, l'autre veut durer. `CNetSubSystem` peut servir les
  deux, mais l'initialisation doit alors être avancée avant `InitAll()`.
- **Le coût est un délai au démarrage** : DHCP, puis DNS, puis NTP. Sur un réseau absent ou lent, c'est du
  temps où l'utilisateur regarde un écran vide. **À borner et à rendre facultatif** : un appareil qui
  attend un réseau qui n'existe pas est pire qu'une horloge fausse. Une préférence `ntp` vide veut dire
  « n'essaie même pas », et un délai maximum court (deux ou trois secondes) doit être respecté.
- **L'ordre de la chaîne rend d'ailleurs l'attente inutile dans le cas courant** : avec un RTC alimenté,
  l'heure est déjà juste au démarrage et NTP n'a qu'à confirmer — donc il peut très bien le faire
  **après** que le Mac a démarré, en tâche de fond, et n'écrire que le RTC. C'est la conception à viser :
  NTP ne devrait jamais être sur le chemin critique du démarrage.
- **Le Pi 1/2/3 met son Ethernet sur l'USB**, qui alimente aussi le clavier et la souris (§Pitfalls). Une
  synchronisation NTP au démarrage est trop brève pour gêner, mais un démon qui interroge en boucle ne
  l'est pas : espacer largement, et mesurer avant de croire que c'est gratuit.
- **Wi-Fi** : `addon/wlan` existe dans Circle, mais il demande le firmware Broadcom sur la carte et une
  configuration (SSID, clé) qu'il faudra bien mettre quelque part — donc dans les préférences, avec la
  question du stockage d'un secret en clair sur une carte FAT. À traiter en phase 13, pas ici.

**Ordre d'implémentation** : 2 d'abord — faite. Puis 1 quand il y a une carte. Puis 3 après la phase 13,
et hors du chemin critique. Préférences prévues : `rtc` (auto ou off), `ntp` (serveur, vide pour aucun),
en plus de `timezone` qui existe.

#### Ce qui reste dû à la sécurité des données

- L'écriture du RTC par le Mac reste perdue (`emul_op.cpp:181`). Tant qu'il n'y a pas de RTC matériel,
  l'implémenter n'aurait nulle part où écrire ; avec un RTC, « régler la date » dans le tableau de bord
  devrait descendre jusqu'à `CRealTimeClock::Set()`. C'est la case « patch d'écriture d'horloge ».
- Une horloge qui recule entre deux démarrages produit des volumes dont `drLsMod` précède `drCrDate`,
  l'état exact que `fsck_hfs` appelle « MDB needs minor repair ». Toute source ajoutée doit donc passer
  par le même plancher que la brique 2 : **on ne recule jamais**, quel que soit ce que dit la source.

#### La PRAM

**Faite le 2026-08-29** (`src/circle/xpram_circle.cpp`, remplaçant `xpram_dummy.cpp`). 256 octets qui
tiennent ce que le Mac retient d'une session à l'autre : disque de démarrage, volume sonore, suivi de la
souris, motif du bureau.

La question ouverte est tranchée : le chemin relatif de `xpram_dummy` **atterrissait bien à la racine de
la carte**, le répertoire courant de FatFs étant la racine du volume monté. Mais c'était un accident, pas
une décision — le fichier est maintenant nommé `/BasiliskII_XPRAM` explicitement.

Deux corrections de fond :

- **Écriture dès que la PRAM change**, et non au seul arrêt propre comme en amont. Un fichier de 256
  octets comparé une fois par seconde ne coûte rien, et perdre les réglages du Mac sur une coupure était
  évitable. C'est la case « `xpram_dirty` et écriture différée » du plan, faite sans rustine sur
  `external/`.
- **Appelé depuis `VideoInterrupt()`**, pas depuis le tick. Ce n'est pas sa place par le sujet, mais
  c'est le seul appel périodique qui s'exécute **dans le fil 68k** : le gestionnaire de tick tourne au
  niveau IRQ, où bloquer sur la carte SD est interdit. `VideoInterrupt()` est appelé par
  `emul_op.cpp:467`, donc dans le même contexte que `Sys_write`.
- **Un fichier court est refusé** : cela veut dire que la dernière écriture n'a pas abouti. On repart
  d'une PRAM vide, ce que fait un Mac dont la pile est morte, plutôt que de prendre un fichier partiel
  pour des réglages.

Anecdote instructive rencontrée en chemin : le `BasiliskII_XPRAM` trouvé sur la carte portait la date de
**compilation du noyau qui l'avait écrit**, pas celle du jour — l'illustration la plus courte de pourquoi
la chaîne d'horloge ci-dessus valait le détour.

- [ ] la chaîne NTP → RTC → SD ; il n'en existe pour l'instant que le dernier maillon de secours
- [x] source journalisée au démarrage : `Clock: Aug 29 14:29:40 (UTC+2:00), from the build time — no RTC
      and no NTP yet`. Une horloge fausse doit se voir dans le journal, pas se deviner
- [x] `timezone` en préférence (minutes à l'est d'UTC), appliqué avant `SetTime()`
- [x] **repli sur l'heure de compilation**, posé maintenant dans l'horloge de Circle elle-même plutôt que
      dans `TimerDateTime()` : sans RTC ni NTP, `CTimer::GetTime()` compte depuis zéro, donc
      on annonçait 1970 + quelques secondes et le Mac estampillait le volume en **1904**. Ce n'était pas
      cosmétique : `drLsMod` se retrouvait antérieur à `drCrDate`, un état impossible, et c'est ce que
      `fsck_hfs` appelle « MDB needs minor repair ». Mesuré avant : `drLsMod` 2082844811 contre
      `drCrDate` 3612702325. Après correction : ordre correct. **Cela ne débloque pas le démarrage** — le
      refus tient au seul bit 8 — mais cela cesse d'abîmer les dates des volumes de l'utilisateur
- [ ] **idée à creuser** : à défaut de RTC et de NTP, repartir de la **dernière heure connue du volume**,
      c'est-à-dire `drLsMod` du MDB. `HfsInspect` le lit déjà au démarrage : le volume se souvient à peu
      près de quand il a servi, ce qui est une bien meilleure borne que l'heure de compilation et suit
      l'usage réel de la machine
- [ ] patch `xpram_dirty` et écriture différée
- [ ] **écriture d'horloge, par le crochet qui existe déjà et non par un patch**. Le trap `ClkNoMem`
      ($A053) est patché en `M68K_EMUL_OP_CLKNOMEM` sur notre ROM (`rom_patches.cpp:1188`, branche
      « ROM23/26/27/32 »), donc l'écriture passe par `EmulOp()`, que `xpram_hook_circle.cpp` enveloppe
      déjà. Capturer `d1`/`d2` **à l'entrée** du wrapper — `EmulOp` finit par `r->d[1] = r->d[2]` —,
      reconstituer les quatre octets écrits un par un, puis poser l'heure dans `CFirmwareRTC` s'il
      répond, sinon `CMCP7941X`, sinon un fichier sur la carte. Cette valeur explicite prime sur le
      plancher `drLsMod` de `RefineClock()`, sinon reculer l'horloge volontairement ne survit pas au
      démarrage suivant. C'est ce qui rend inutile tout panneau date/heure dans le firmware (§7.12)
- [x] arrêt propre sur « Shut Down » : `QuitEmulator()` appelle `m68k_emulop_return()`, la boucle 68k rend
      la main, `ExitAll()` ferme l'image, le noyau s'arrête et QEMU quitte via le semihosting
- [x] essai de coupure brutale : `scripts/run-test.sh` termine par `SIGKILL` puis vérifie au `fsck_hfs`,
      en signalant le volume d'écritures réellement effectuées
- vidage périodique du cache disque : **écarté**, les écritures invité descendent déjà en direct
      (`disk_write` → `CDevice::Write`), un `fsync` par écriture n'achèterait qu'une date de modification
      contre de l'amplification d'écriture sur la carte

### Phase 11 — Mesures de référence

Avant toute optimisation. À mesurer : MIPS 68k, part du temps CPU par poste (émulation, vidéo, audio,
disque), images par seconde, images Mac réellement modifiées, bande passante du framebuffer, latence de la
souris, température, fréquence CPU.

**Premier relevé, 2026-08-27, QEMU `raspi3b` sur MacBook Pro M4**, 640×480 8 bits affiché en 1280×960 :

| | headless | fenêtre `cocoa` |
|---|---|---|
| composition | 236 µs, 1,3 % du mural | 452 µs, 2,2 % |
| écran | 55 images/s | 50 images/s |
| invité | 20 162 k opcodes/s | 23 584 k opcodes/s |

Speedometer 4 dans l'invité, colonne « This Machine » contre l'enregistrement « Mac Quadra 650 » :
CPU 44,24 (Quadra 1,32) · Graf 34,92 (1,31) · **Disk 9,61 (1,93)** · Math 473,19 (19,17) ·
**FPU Ave 9,03 (1,01)** · Color Ave 47,24 (1,08). Les deux points faibles relatifs sont cohérents avec
l'état du portage : le chemin disque n'a jamais été optimisé, et `fpu_uae` est une FPU logicielle.

Réserve : entre un run réglé sur 0:60 et un sur 0:480, tous les scores varient d'un facteur 8, soit
exactement le rapport des durées. L'horloge invité n'est pas en cause — relevé sous charge pendant le
bench : Mac Ticks 30894 pour ~30600 attendus à 519 s. À reproduire avant d'enquêter.

Repères d'époque : Speedometer, Norton System Info, MacBench, décompression StuffIt, copies de fichiers,
Doom. Point de comparaison connu : M5Tab atteint 2-3 MIPS sur un RISC-V à 400 MHz, soit un Quadra 610.

### Phase 12 — Vidéo optimisée

1. synchronisation verticale
2. [x] **suivi des zones modifiées** — fait, par comparaison plutôt que par marquage à l'écriture.
   Grille 16×16, copie d'ombre, `memcmp` par tuile, seules les tuiles changées sont converties et
   écrites. Un redessin complet est forcé au changement de mode et de palette. Relevé sous QEMU
   `raspi3b`, 640×480 affiché en 1280×960 : composition **6 191 → 236 µs** en headless (35,5 % → 1,3 %
   du mural), et **74 637 → 452 µs** avec fenêtre `cocoa` (98,7 % → **2,2 %**), écran à 50 images/s,
   invité à **23 584 k opcodes/s**. Marquer les tuiles au moment de l'écriture 68k reste possible mais
   n'a plus d'urgence : comparer 300 Ko coûte bien moins qu'écrire 2,4 Mo dans un framebuffer suivi
   page à page par QEMU. Le modèle d'amont était `update_display_dynamic()`
   (`Unix/video_x.cpp:2343`), le mode « Dynamic » du menu Window Refresh Rate, soit `frameskip = 0`.
   Il découpe l'écran en grille 16×16, garde une copie d'ombre, compare par `memcmp` boîte par boîte
   en étalant le balayage sur 8 ticks, ne pousse que les boîtes modifiées et fusionne les boîtes
   voisines en bandes. Le comparer coûte bien moins que convertir : la cible est 60 Hz sous les 3 %
   de temps mural, contre 8,5 % aujourd'hui à 55 Hz
3. double tampon si le modèle le permet
4. composition sur le cœur 2 (**S2**) si les mesures le justifient
5. modes 16 et 32 bits (Thousands, Millions)
6. sur Pi ≤ 4 seulement, chemin direct 8 bits avec palette matérielle, **en option**

Objectif Doom : la logique du jeu tourne à 35 Hz ; viser **35 mises à jour par seconde stables** sur une
sortie 60 Hz avant d'envisager quoi que ce soit d'autre.

### Phase 13 — Réseau

- [ ] `ether_circle.cpp` avec partage de la MAC du Pi
- [ ] respecter `nonet` au démarrage ; ne créer un contrôle firmware que si un vrai choix de mode ou
      d'interface apparaît, pas pour un simple activé/désactivé
- [ ] NTP au démarrage puis passage de la main au Mac
- [ ] essais : DHCP côté Mac, TCP/IP, transfert de fichiers, navigateur d'époque

### Phase 14 — Audio

- [x] couche plateforme écrite (`audio_circle.cpp`) : 44100 Hz, 16 bits, stéréo, file d'attente de
      100 ms drainée par le DMA de Circle. Le tick 60 Hz lève `INTFLAG_AUDIO` tant que la file a de la
      place ; `AudioInterrupt()` s'exécute alors dans le contexte 68k — obligatoire, puisqu'il fait
      tourner du code 68k pour interroger le mixeur du Mac — et le bloc obtenu est permuté en
      petit-boutiste (le Mac émet du gros-boutiste, Circle attend l'inverse). `libsound.a` n'est pas
      dans la ligne d'édition de liens de circle-stdlib : ajoutée côté Okapia
- [ ] **rien n'est audible sous QEMU** : `raspi3b` n'émule aucune sortie son, et prétendre le contraire
      a gelé le Mac — le son d'alerte système promis puis jamais rendu, suivi d'un arrêt brutal et d'une
      carte perdue. Le son est donc coupé par défaut sous `CIRCLE_QEMU` ; `-DOKAPIA_FORCE_SOUND` le
      rallume pour exercer la plomberie. La vérification à l'oreille demande du matériel réel (phase 2)
- [ ] sortie HDMI plutôt que PWM (le jack n'existe pas sur Pi 5), contrôleur de volume et de coupure
      réellement appliqués — pour l'instant le Mac les demande, on les mémorise sans les appliquer
- [ ] brancher le contrôle `nosound` du firmware sur cette initialisation, avant `InitAll()`
- [ ] essais : sons système, lecture AIFF, jeux

### Ce que la coupure brutale abîme réellement — mesuré

Enquête du 2026-08-28, base certifiée saine par Disk First Aid, traces `--wrap` sur `Sys_read` et
`Sys_write`.

| fait | mesure |
|---|---|
| écritures perdues | **aucune** — 39 écritures pour atteindre le Finder, toutes complètes |
| dégâts après coupure | `drAtrb 0000` + « MDB needs minor repair », rien d'autre |
| refus ou lenteur ? | **refus** : 346 lectures en 0,44 s, puis plus rien pendant 37 s |
| ce que le Mac examine | catalogue (176 nœuds) entrelacé de la bitmap — un contrôle de cohérence |
| écritures pendant ce contrôle | **zéro** : il ne tente aucune réparation |
| flush périodique | oui, MDB + nœuds sales réécrits ~50 s après la fin du démarrage |
| coupure *après* le flush | **échoue aussi** : le bit reste à zéro tant que le volume est monté |
| bit 8 de `drAtrb` reposé à la main | **démarre**, et MacOS affiche quand même « pas éteint correctement » |

Conclusion : les données survivent, le volume est structurellement intact, et **un seul bit conditionne le
démarrage**. Le dialogue d'arrêt incorrect vient d'ailleurs (fichier Système ou PRAM), donc ce bit ne sert
qu'à cela. Le comportement est identique à Basilisk sur macOS, où un plantage fait sauter le volume 7.6 au
profit du 8.1 — ce n'est donc pas propre à Okapia.

**Piste de l'encapsulation : écartée par l'expérience.** Le même volume 7.6, enveloppé dans un conteneur
avec table de partition Apple (`ER` + `PM` + `Apple_HFS`), démarre normalement puis **refuse exactement
pareil** après une coupure brutale. Table de partition et pilote embarqué ne changent rien : Basilisk ne
charge jamais le pilote du disque, il substitue son `.Disk` par patch ROM. Seule la table compte, et
seulement pour situer le volume (`find_hfs_partition`, `disk.cpp:120`).

**Ce qui est vérifié en revanche** : un volume sale est refusé **au démarrage** mais monté sans difficulté
**en disque secondaire**, et ce montage le répare. Démontré ici : carte à deux disques, démarrage sur un
volume propre, le volume sale apparaît sur le bureau et MacOS y écrit (7 octets, `drAtrb` 0000 → 0003).
C'est le cycle observé de longue date sous Basilisk, reproduit sur notre pile.

**Observé au niveau Toolbox** (trace `op_illg_1`, `OKAPIA_TRACE=1`) : les deux démarrages sont identiques
jusqu'au trap #1886, puis divergent nettement.

| | volume propre | volume sale |
|---|---|---|
| après `MountVol` (#1870) | lectures puis **`Write` (#1893)** | lectures seulement |
| appels `MountVol` | 4 | 2 |
| écritures | oui | **aucune** |
| fin | démarre | arrêt à 2,98 s, trap #8633 |

`MountVol` écrit sur un volume propre — c'est MacOS qui efface le bit 8 pour le marquer en usage. Sur un
volume déjà marqué, il n'a rien à écrire, part sur une autre branche, lit 442 fois et renonce. **Le refus
vient du Gestionnaire de fichiers de MacOS**, pas de Basilisk ni de la couche Okapia.

**Réponse obtenue** — en surveillant `ioResult` dans le bloc de paramètres après le trap :

| volume | `MountVol` | code |
|---|---|---|
| propre | trap #1870 | **`noErr`** |
| sale, seul sur la carte | trap #1868 | **`badMDBErr` (-60)** — *bad master directory block* |

Un seul appel, un seul verdict. **MacOS valide le MDB, le juge indigne de confiance parce que le volume
est marqué en cours d'usage, et refuse le montage.** Ce n'est pas un bug : c'est une politique de sûreté
délibérée du Gestionnaire de fichiers, et elle explique tout ce qu'on observe depuis le début.

**Correction d'une conclusion antérieure** : envelopper le volume dans un conteneur partitionné ne change
rien, mais cela ne teste **pas** l'hypothèse du pilote. Basilisk ne charge jamais le pilote embarqué du
disque — il substitue son `.Disk` par patch ROM. Sur matériel réel, la ROM charge ce pilote et le fait
participer au montage. Cette piste-là reste donc **non testée**, pas écartée, et c'est la seule qui puisse
encore expliquer qu'un vrai Mac redémarre après une prise arrachée.

Aucune issue correspondante en amont (`kanjitalk755/macemu`) : les utilisateurs contournent avec plusieurs
disques plutôt qu'ils ne le signalent.

### Ce que fait BlueSCSI, lu dans son code

`reference/BlueSCSI-v2` (GPLv3, comme nous — code réutilisable). Trois enseignements.

**Sa durabilité vient de l'absence de cache, pas d'un garde-fou.** `SYNCHRONIZE CACHE` (0x35) :
« We don't have a cache. do nothing. » Et le vidage en fin d'écriture porte le commentaire « Normally
does nothing as we do not change image file size and data writes are not cached »
(`BlueSCSI_disk.cpp:2740`). **C'est exactement notre modèle**, déjà mesuré : écriture directe, aucun cache
entre l'invité et la carte, 39 écritures sur 39 complètes.

**Il ne connaît rien au système de fichiers invité.** Aucune trace de MDB, de `drAtrb`, de réparation ou
de *scavenge*. Il ne répare jamais un volume : il n'en a pas l'idée.

**Son garde-fou est un contrôle de format, pas de coupure.** `QuirksCheck.cpp` refuse les volumes HFS nus
— « This is a bare HFS Volume. Use DiskJockey to convert it to a Device image. » — puis valide le pilote
SCSI déclaré par le DDR et met en garde contre le pilote LIDO. Conséquence directe : **les utilisateurs de
BlueSCSI ne font jamais tourner que de vraies images de périphérique**, avec table de partition et pilote
chargé par la ROM du Mac.

Ce qui rouvre — sans la trancher — la seule hypothèse restante : sur matériel, le pilote du disque
participe au montage, et Basilisk ne le charge jamais. Nous ne pouvons pas reproduire ce chemin.

**À reprendre chez eux** : le contrôle de format à l'ouverture. Notre `HfsInspect` fait déjà la moitié du
chemin ; refuser (ou au moins signaler) un volume nu là où un vrai Mac attend une image de périphérique
serait la même prudence.

### Réparer le volume nous-mêmes — **fait**, via `libhfs`

`libhfs` (Robert Leslie, 1996-1998 ; fork maintenu par Pablo Lezaeta) fait déjà exactement ce qu'il faut,
et sous une licence **GPLv2 or later**, donc compatible avec notre GPLv3.

- **`volume.c:440`** : au montage, si `HFS_ATRB_UMOUNTED` est absent, il lance `v_scavenge()`. C'est
  précisément le traitement que subit un volume sale monté depuis un autre Système.
- **`volume.c:140`** : au démontage, il repose le bit.
- Donc un simple **`hfs_mount()` en lecture-écriture suivi de `hfs_umount()`** suffit à rendre un volume
  amorçable. `hfsck` (1705 lignes, contrôles plus profonds du MDB et des B-trees, interactif via `ask()`)
  vient en second temps, pas d'emblée.

**Portage** : `libhfs` fait 7649 lignes et isole tout le système dans `os.h` — `os_open`, `os_read`,
`os_write`, `os_seek`, `os_close`. Ses `.c` n'incluent aucun en-tête système. Le portage se réduit donc à
un seul fichier implémentant ces cinq fonctions sur notre couche fichier, exactement comme la couche
plateforme de Basilisk.

**Fait le 2026-08-28.** `external/hfsutils` en sous-module, `libhfs` compilé dans le noyau (14 objets,
48 Ko), `os.c` inchangé sur newlib. Au démarrage : `HfsInspect` lit le bit de démontage, et s'il manque
`HfsRepair` monte le volume en lecture-écriture — ce qui déclenche le scavenge — puis le démonte, ce qui
le remarque propre.

```
okapia-hfs: marked in use (drAtrb 0000) — the last session did not shut down
okapia-hfs: repairing
okapia-hfs: "Mac HD 7.6", 467904 KB free of 511984 KB, 419 files, 45 folders, System folder 714
okapia-hfs: repaired and marked clean
```

Vérifié à l'écran : après une coupure par `SIGKILL`, le Mac démarre et affiche le dialogue d'époque
« Cet ordinateur n'a pas été éteint correctement ». **Plus besoin de second volume ni de reconstruction de
carte.** Reste à faire : le dialogue de confirmation (phase 16) et l'exercice par `run-test.sh` avant que
ce soit considéré comme acquis.

**Ce qui reste écrit à la main, et pourquoi** : la lecture du bit de démontage doit précéder tout montage,
puisque monter le répare ; et la localisation du volume doit suivre celle de Basilisk
(`find_hfs_partition`), pas celle de libhfs, puisque c'est Basilisk qui servira le volume au Mac. Tout le
reste — nom, géométrie, comptes, dossier Système béni — vient de `hfs_vstat()`.

**Contrainte** : c'est du code qui écrit dans le volume de l'utilisateur. Il tombe donc entièrement sous
§Data safety — jamais de réparation sur un volume qu'on ne sait pas valider, et `scripts/run-test.sh` doit
l'exercer avant qu'il ne soit activé par défaut.

### Pré-contrôle du volume au démarrage — fait

`src/circle/hfs_volume_circle.cpp` lit le MDB avant de lancer le 68k et annonce ce que le Mac va trouver :
nom du volume, géométrie, blocs libres, `drAtrb`. Si le volume est marqué en cours d'usage, il le dit et
prédit l'échec (`MountVol` → `badMDBErr`) au lieu de laisser apparaître une disquette inexpliquée. Il
trouve la partition `Apple_HFS` comme le fait Basilisk, donc il regarde bien le même volume.

**Lecture seule, délibérément.** Réparer est une décision distincte : un contrôle qui tamponnerait un
volume abîmé serait pire que pas de contrôle (§Data safety). Un réparateur digne de ce nom devrait
parcourir la bitmap et le catalogue, recalculer `drFreeBks`, et ne reposer le bit 8 que si tout concorde
— c'est-à-dire refaire le travail de Disk First Aid.

### Le substitut `.Disk` est hors de cause — vérifié

Sur matériel, la ROM charge le pilote du disque depuis sa partition et ce pilote participe au montage.
Basilisk substitue `.Disk` par patch ROM et ne charge jamais celui du disque — c'est la seule différence
structurelle qui reste. La trace `op_illg_1` rapporte désormais le csCode et l'`ioResult` de chaque `Control` et `Status`, comme
pour `MountVol`. Verdict : sur le montage qui échoue, **`badMDBErr` est la seule valeur non nulle de tout
le run**. `Status` csCode 8 (DriveStatus) et `Control` csCode 9 rendent `noErr`, exactement comme sur un
montage réussi. Le pilote ne refuse rien et n'est pas sollicité différemment.

**Toutes les hypothèses côté émulateur sont donc épuisées** : écritures perdues (non), dégâts structurels
(non), table de partition (non), pilote embarqué non chargé (sans effet mesurable), refus du pilote
substitué (non). Le refus est une décision de `MountVol` fondée sur le seul contenu du MDB : volume
marqué en cours d'usage, donc MDB jugé non fiable. Reposer le bit 8 seul suffit à le faire accepter.

### Volume de secours — contournement, à ne retenir que si la question ci-dessus reste sans réponse

Un Mac refuse de **démarrer** sur un volume marqué en cours d'usage, mais le **monte** sans difficulté en
disque secondaire, et ce montage déclenche le *scavenge* HFS qui le répare. Constaté de longue date sous
Basilisk : un plantage salit le volume 7.6, le lancement suivant démarre sur 8.1, qui monte et répare 7.6,
et après un arrêt propre 7.6 redémarre seul. Okapia reproduit ce comportement à l'identique — ce qui lui
manque n'est pas la correction mais le **second volume amorçable**.

- [ ] embarquer un petit Système de secours comme seconde préférence `disk` (`disk.cpp:161` boucle déjà
      sur plusieurs entrées)
- [ ] le monter **en lecture seule** (préfixe `*`), pour qu'aucune coupure ne puisse le salir à son tour
- [ ] essai : salir le volume principal, vérifier que le secours démarre, monte et répare, puis qu'un
      arrêt propre rend le principal amorçable
- **Écarté** : forcer `drAtrb` nous-mêmes. Cela masquerait une vraie corruption, exactement l'arbitrage
      que §Data safety refuse.

### Phase 15 — Séquence de démarrage Macintosh

- [ ] analyse de la carte de ressources du dump ROM, extraction des ressources `snd ` et des icônes
- [ ] carillon joué **le plus tôt possible**, avant l'émulateur (chemin audio minimal initialisé tôt)
- [ ] Happy Mac quand tout est en place
- [ ] Sad Mac, carillon de mort et **codes d'erreur documentés** : ROM absente, ROM non reconnue, aucun
      disque amorçable, SD illisible
- [ ] aucun de ces éléments n'est versionné : tout vient de la ROM de l'utilisateur

### Phase 15bis — Choisir son Système parmi les images de la carte

Le besoin : plusieurs images sur la carte SD, et l'on choisit laquelle démarre. Trois briques. **Les deux
premières sont faites** (2026-08-29) : la machine se configure depuis la carte et annonce ce qu'elle
porte, sans une ligne d'interface. **La troisième aussi** : le `modelid` se déduit du Système installé.
Reste l'habillage en phase 16.

**1. Lire la configuration depuis la carte** — c'était la case non faite de la phase 3, et le préalable de
tout le reste. Fait le 2026-08-29 : `src/circle/prefs_circle.cpp` remplace `prefs_dummy.cpp`, qui cherchait
son fichier relativement à un répertoire courant que le noyau ne pose jamais — il n'en trouvait donc
aucun. Le format, les mots-clés et l'analyseur restent ceux de Basilisk (`LoadPrefsFromStream`,
`SavePrefsToStream`) : un `BasiliskII_Prefs` écrit par un Basilisk de bureau se lit ici, et
réciproquement.

Trois points valaient d'être notés :

- Les défauts d'Okapia sont dans `AddPlatformPrefsDefaults()`, que `PrefsInit()` appelle **avant**
  `LoadPrefs()` — la carte écrase donc les défauts sans une ligne de code pour l'organiser.
- Sauf pour les items « multiples » (`disk`, `floppy`, `cdrom`), que l'analyseur **ajoute** au lieu de
  remplacer : un défaut posé avant la lecture survivrait à côté de la valeur du fichier et le Mac verrait
  les deux. Ceux-là sont posés après lecture, seulement s'ils manquent.
- `ramsize` décide de l'allocation du bloc Mac, donc la carte doit être montée **avant** elle. C'est la
  seule chose qui passe désormais devant l'invariant « le bloc de 257 Mo d'abord ». Mesuré : 935 Mo de
  tas libre sur une carte 1 Go, exactement le chiffre relevé quand la mesure précédait le montage.

- [x] préférences lues et écrites sur la carte, au format Basilisk (`/BasiliskII_Prefs`)
- [x] `disk`, `rom`, `ramsize`, `modelid`, `cpu`, `fpu`, `frameskip`, `nosound` viennent du fichier ; les
      constantes du noyau sont devenues des défauts
- [x] fichier créé au premier démarrage avec des exemples en commentaire (`#` en première colonne)
- [x] deux options propres à Okapia : `hfsrepair` et `hfsinventory`
- [x] options documentées dans `README.md`, y compris celles de Basilisk **sans effet** sous Circle et
      pourquoi

**2. Décrire les images présentes** — sans quoi un choix se fait à l'aveugle sur des noms de fichiers.
Fait le 2026-08-29, dans `hfs_volume_circle.cpp`. Monté en lecture seule, `libhfs` n'écrit rien du tout,
pas même le scavenge (`volume.c:1059`) : l'inventaire décrit les volumes sans en toucher un.

- [x] balayer la carte, retenir les fichiers que `libhfs` ouvre comme volumes HFS
- [x] pour chacun : nom du volume, taille, place libre, nombre de fichiers et de dossiers, amorçable ou
      non (`blessed` non nul), propre ou monté
- [x] journaliser cet inventaire au démarrage, avant toute interface
- [x] repli : si aucun `disk` configuré n'existe sur la carte, démarrer sur le premier volume amorçable
      de l'inventaire plutôt que de s'arrêter sur une disquette au point d'interrogation

**3. Poser le `modelid` assorti au Système** — la leçon du 2026-08-29 : un System 7.1 muni de son enabler
s'arrête sur un cadre vide si on lui annonce un Quadra 900. Le `modelid` suit le Système, pas la ROM, et
deux Systèmes de familles différentes ne peuvent pas partager une configuration. Fait le 2026-08-29,
`HfsSystemVersion()` dans `hfs_volume_circle.cpp`, appelé avant `InitAll()` puisque `rom_patches.cpp` lit
`modelid` en rustinant la ROM.

**Le fichier Système se trouve par type et créateur (`zsys` / `MACS`), jamais par son nom** : le nom est
localisé, un Système français s'appelle `Système`. Le volume 7.6 de test l'a d'ailleurs gardé en anglais,
donc chercher « System » aurait marché ici et cassé ailleurs — exactement le genre de réussite trompeuse
qu'il faut éviter.

Relevé sur les deux images :

```
/machd76.image: "System" says System 7.6.1 (F1-7.6.1) — model 5
/boot71.img:    "System" says System 7.1.0 (7.1) — model 5, overriding the preferences
```

- [x] version lue dans la ressource `vers` du fichier Système via `libhfs` (fork de ressources en lecture
      seule : `hfs_setfork` tronque, mais `f_trunc` sort immédiatement sur un volume monté en lecture
      seule, `file.c:16`)
- [x] `5` avant 8.0, `14` à partir de Mac OS 8 — 7.5 et 7.6 tournent sur les deux et sont testés sur `5`,
      donc la frontière est à 8.0 et nulle part ailleurs
- [x] optionnel : `modelidauto` (défaut vrai) ; à faux, la valeur du fichier est prise telle quelle
- [x] échec de détection journalisé, pas passé sous silence — la valeur du fichier sert alors de repli

**Reste à faire** : l'interface de la phase 16 présente ce que 2 a découvert et laisse choisir — elle en
est l'habillage, pas la fondation. Le choix se pose alors dans `disk` et se relit au démarrage suivant,
sans autre mécanisme.

### Phase 16 — Firmware Okapia

- [ ] `hal_circle` consolidé et utilisable hors émulateur
- [x] pas LVGL. Le tampon est réclamé par `CBcmFrameBuffer (0, 0, 32)` comme le fait le compositeur ;
      `C2DGraphics` reste à prendre le jour où l'interface bougera et voudra un double tampon, un écran
      fixe n'en ayant pas besoin. Noir, blanc et **gris pleins** à la profondeur de sortie
- [x] **primitives sur une surface** — pointeur, dimensions, pas — dans `src/firmware/okapia_gfx.*`,
      sans un seul type Circle. Toile logique en 8 bits sur une palette de quatre entrées : 300 Ko au lieu
      de 1,2 Mo, et l'expansion vers la sortie se fait une fois, par table (`okapia_present.cpp`)
- [x] inversion vidéo, roundrect et mesure de texte : les trois manques face à QuickDraw, écrits. Une
      seule table d'entames sert le rempli et le contour, sinon un bouton dépasse de son propre trait ;
      les pixels sont testés en leur centre, ce qui sépare un coin rond d'un coin encoché
- [x] **pas de motif tramé** : rien n'en a eu besoin, et la primitive n'existe donc pas
- [x] **thème = structure constante de fonctions *et de métriques*** (`okapia_theme.*`) ; aucun composant
      ne connaît une hauteur de bouton, il la demande
- [ ] **thème uchronique, aucun système mesuré** : le menu s'ouvre devant System 6 à Mac OS 9, donc le
      chrome ne se date pas. Plat — ni biseaux Platinum, ni gel Aqua —, rectangles arrondis, double cerclé
      du bouton par défaut réutilisé pour le focus, ascenseur sans flèches. Les icônes portent l'époque,
      pas le cadre
- [x] composants avant écrans (`okapia_widgets.*`) : cadre de dialogue, étiquette, bouton et bouton par
      défaut, bouton à icône, case, radio, liste, ascenseur, menu local, champ de saisie, barre de
      progression, liseré de focus, séparateur
- [ ] restent le cadre d'alerte et le repli à la ligne des étiquettes, que le dialogue de réparation
      demandera le premier
- [x] **écran de spécimen** : chaque composant dans chaque état, sous QEMU (`scripts/specimen.sh`) et en
      fichier sur l'hôte (`tests/host`). Comme rien n'est copié d'un système existant, il est le seul
      arbitre du thème — d'où sa place avant le sélecteur, pas après
- [x] largeur des boutons calculée depuis leur propre libellé et le rembourrage du thème
- [ ] modèle de boîtes complet en lignes et colonnes ; le spécimen pose encore ses ordonnées à la main
- [ ] focus clavier complet — Tab et Shift-Tab, flèches en liste, Espace et Retour, Échap
- [x] toile logique 640×480 agrandie d'un facteur entier et centrée, même méthode que
      `video_circle.cpp:254-262` ; vérifié à ×1 sur 640×480 et à ×2 sur 1280×960
- [x] blitter de glyphes à largeur variable, plus gras et italique synthétisés à la QuickDraw
- [x] `scripts/gen-font.py` convertit les fontes de Circle au format du projet, qui porte déjà une avance
      par glyphe — la colonne qu'une proportionnelle remplira sans toucher au C++
- [ ] proportionnelle bitmap X11, **jamais Chicago** ; `Font8x16`/`Font8x12` tiennent la place en attendant
- [x] `œ`, `Œ`, `…` et `’` absents d'ISO-8859-1 : les sources restent en UTF-8 et `GfxText` décode, en
      repliant ces signes sur leurs ancêtres ASCII. Cela évite aussi le piège du littéral `"\xE9"`, qui
      avale la lettre suivante quand elle est un chiffre hexadécimal
- [ ] liste des systèmes avec détection automatique du modèle compatible, et état propre/sale par volume
- [ ] icônes de dossier monochromes tracées depuis les références System 6, System 7 et Mac OS 8/9
- [ ] colonne radio « par défaut », indépendante de la sélection courante, qui place son `disk` en premier
- [ ] cases « lecture seule » (préfixe `*`, `disk.cpp:161`) et « monter » par ligne
- [ ] réglages derrière un bouton à glyphe : RAM, rafraîchissement, partage, nom du volume partagé,
      audio et langue — **pas de date ni de fuseau**, l'horloge se règle dans Mac OS (§7.7)
- [ ] audio en quatre états — coupé / HDMI / jack / USB — et non un booléen (`audio_circle.cpp:70`)
- [ ] boutons Éteindre et Oublier la PRAM, plus Cmd-Option-P-R pendant la fenêtre de démarrage
- [ ] volet d'informations : ROM et modèle retenus, carte, volumes, volume de démarrage, `modelid` et son
      éventuel échec de détection, date de compilation, modèle de Pi, mode réellement accordé
- [ ] marque `*` et action « Enregistrer et redémarrer » pour toute préférence non applicable à chaud
- [ ] aucune résolution dans les réglages : sortie détectée, mode logique possédé par Moniteurs et la PRAM
- [ ] aucun réseau tant qu'il n'existe pas un choix utile au-delà d'activé/désactivé
- [ ] ne pas masquer une préférence cible parce que sa valeur est encore en dur ; planifier son branchement
- [ ] anglais et français par une table de chaînes du projet ; langue persistée dans les préférences
- [ ] écriture dans le seul `BasiliskII_Prefs`, qui reste la source de vérité
- [ ] fond gris uni (~`#BCBCBC`) pendant environ deux secondes ; `Option` seule ouvre le sélecteur
- [ ] ouverture automatique si la configuration manque ou si aucun système n'est amorçable

**Ce que la phase 16 hérite, après nettoyage** — `src/firmware/` : surface et primitives antialiasées,
fonte et son échelle de tailles, icônes peintes, thème avec ses métriques, composants avec leur test de
clic, et l'écran de spécimen. 2 174 lignes, plus le noyau de démonstration et deux contrôles sur l'hôte.
Les fontes sont dans `assets/fonts/` et les dossiers Système dans `assets/icons/` : une entrée de
compilation ne vit pas dans un dossier de référence, ce que l'AGENTS.md dit déjà de `reference/`. La
maquette reste dans `boot-menu/` comme archive.

Trois choses ont été retirées après revue : la planche de variantes, dont les choix sont faits et qui
maintenait un second exemplaire du dessin de chaque pièce — elle était en outre **liée dans le noyau du
Pi**, un `$(wildcard)` sur le répertoire du firmware suffisant à embarquer un outil d'atelier dans
l'appareil ; les métriques d'ombre, mortes depuis que l'ombre du dialogue a sauté ; et les styles
synthétiques, la graisse étalée et l'italique cisaillée n'ayant plus lieu d'être face à une vraie graisse
dessinée — et à un vrai oblique (`helvO`) si l'italique revient.

**Le spécimen tient sur deux pages, parce qu'un écran ne suffit pas.** À 640×480, avec un cadre, une marge
et un rythme corrects, un dialogue ne porte pas quarante composants — et raboter le contenu jusqu'à ce qu'il
rentre revient à mesurer l'entassement plutôt que le thème. Quatre tours ont été perdus à rogner une liste,
des lignes de texte, une étiquette, en cassant autre chose à chaque fois. **Un spécimen est un document, et
un document se pagine** : les contrôles d'un côté, les états et la typographie de l'autre. Le noyau alterne
les deux, `scripts/specimen.sh 2` vise la seconde, et les contrôles tournent sur les deux.

Deux mesures utiles au passage : un redessin plein écran antialiasé coûte près d'une seconde sous QEMU — sans
importance pour un menu qui dessine une fois, mais assez pour faire dériver une cadence à la seconde. Et
l'espacement entre composants n'avait rien à se reprocher ; je l'avais élargi en même temps que je corrigeais
la marge du cadre, deux changements mêlés dont un seul était demandé.

**Un contrôle qui porte un anneau a besoin de la place de son anneau.** Le bouton par défaut était ancré sur
le bord du contenu, et son liseré débordait donc d'exactement sa portée dans la marge que le rectangle de
contenu venait d'établir. `ThemeReach()` répond « combien ce contrôle dessinera hors de lui-même dans cet
état », et une mise en page le demande au lieu de le supposer — ces largeurs appartiennent au thème.

Et la mise en page du spécimen coulait encore d'ordonnées absolues : corriger l'origine de la marge a donc
réparé le haut et la gauche en laissant la moitié basse déborder. Elle s'écoule maintenant depuis le
rectangle de contenu, pied ancré en bas, largeurs en fractions et non en constantes de maquette. **Une mise
en page qui ne tient que pour un jeu de métriques n'est pas une mise en page.** Le contrôle
`CheckSpecimen()` balaie la bande entre le cadre et le contenu et échoue au moindre pixel qui s'y trouve —
c'est ce qui aurait attrapé le débordement le jour même.

**La marge se mesure depuis le filet intérieur, pas depuis l'arête du dialogue.** Elle était appliquée
depuis le bord extérieur, si bien que le cadre lui prenait sa propre épaisseur et que le contenu tenait
moitié moins loin que la métrique ne l'annonçait — d'où une impression d'entassement qui n'était pas un
défaut de valeur mais d'origine. `ThemeContent()` rend le rectangle où un écran peut poser ses éléments, et
tous passent par lui pour que « marge » veuille dire une seule chose. Même famille : **la coche se centre
sur sa propre étendue** et non sur la boîte où ses deux traits ont été calculés, une coche étant bien plus
haute d'un côté que de l'autre.

Le juste équilibre entre l'air et l'occupation utile ne se jugera pas sur cet écran : le spécimen entasse
quarante composants pour les montrer tous, là où un vrai dialogue en portera huit. Les premières fenêtres
de la phase 16 sont l'arbitre, et la valeur à régler est `nMargin` — un seul nombre, dans le thème.

**Le plancher en dur est le piège des métriques sous un autre jour.** Trois défauts trouvés en regardant le
640×480, tous de la même famille : l'icône de réglages posait ses curseurs à une taille minimale de sept
pixels, ce qui est juste à soixante et se chevauche à quatorze — c'est-à-dire précisément la taille que
donne une sortie 640×480 ; l'encoche de la marque d'arrêt valait une fraction du trait qui, arrondie vers le
bas, tombait exactement sur la demi-largeur de la barre, si bien que la barre se soudait à l'anneau ; et les
deux filets du cadre avaient la même épaisseur, ce qui se lit comme une erreur plutôt que comme un cadre.
La règle qui en sort : **une fraction de la pièce, jamais un plancher** — et quand un dégagement doit exister,
il s'écrit comme une clairance minimale d'un pixel, pas comme une fraction dont on espère qu'elle arrondira
bien.

**« Pas propre, mal aligné » était mesurable, et personne ne le mesurait.** D'où `tests/host/check_geometry`,
qui dessine chaque pièce seule sur un fond blanc, relève la boîte d'encre et **échoue si les quatre côtés ne
s'accordent pas** — à cinq échelles, parce qu'une règle vraie à 1:1 et fausse à 2,25 n'est pas une règle. Il
tourne dans `make -C tests/host`, à côté du rendu.

Il a trouvé du premier coup ce que l'œil signalait sans pouvoir le nommer :

- **le centre était pris sur la demi-extension déjà rétrécie** par l'épaisseur du trait, dans le champ de
  distance. Toute forme à contour se trouvait donc décalée d'une demi-épaisseur vers la gauche et vers le
  haut : l'anneau dégageait son bouton d'un pixel de plus à gauche qu'à droite ;
- **la racine carrée maison était fausse.** Trois itérations de Newton parties du carré rendaient 6,6 pour 6,
  donc chaque contour sortait un pixel trop étroit, symétriquement — ce qui se lit comme « pas assez écarté »
  et non comme un bug. Remplacée par `__builtin_sqrtf`, qui est l'instruction FSQRT sur AArch64 et ne
  demande aucune bibliothèque ;
- **la bande de l'anneau était calculée une demi-épaisseur trop loin** : la forme étant déjà rétrécie, sa
  frontière *est* la ligne médiane du trait, donc la bande vaut `|d| <= t/2` et non `|d + t/2| - t/2` ;
- **le texte se centrait sur la cellule**, qui porte une descente dont « Focus » ne se sert pas. Il se centre
  désormais sur la hauteur de capitale, mesurée sur le H à la génération de la fonte.

Après quoi : zéro écart, aux cinq échelles. Reste une limite connue et inscrite comme telle — **rien ne
tronque encore une chaîne à son contrôle**, donc une traduction plus longue que son bouton déborde au lieu
d'être coupée. C'est le travail du modèle de boîtes.

**Deux défauts de rendu qui valaient d'être compris** — trouvés à l'œil, pas par un test :

- **les accents des capitales étaient coupés.** Helvetica déclare `FONT_ASCENT 11` à douze pixels et dessine
  pourtant le É sur douze rangées au-dessus de la ligne de base. Dimensionner la cellule sur la déclaration
  perdait la rangée du haut de tous les accents majuscules, deux rangées en 24 px. La cellule se calcule
  maintenant **sur les glyphes**, pas sur ce que la police prétend ;
- **le 1:1 était flou et les anneaux se brouillaient**, pour une seule et même raison : un contour d'un pixel
  était centré *sur* le bord du rectangle, donc moitié dedans moitié dehors, donc deux colonnes à 50 % de
  gris au lieu d'une colonne noire. Invisible à l'échelle 2, criant à l'échelle 1. Un contour se pose
  désormais **à l'intérieur** de son rectangle.

**Et un piège de compilation qui est déjà dans ce fichier, retombé dans un Makefile neuf.** Les règles
privées de `src/firmware/circle/` n'avaient pas de suivi de dépendances d'en-têtes : ajouter un champ à
`TThemeMetrics` a recompilé le seul `okapia_theme.o` et laissé les autres objets avec l'ancienne disposition
de structure. Le lien a réussi, et le noyau a sauté **dans la table des fontes** — exception d'instruction,
PC au-delà de `_etext`, là où Circle met `PXN=1`. Le symptôme ne ressemble en rien à la cause. Les deux
Makefile portent maintenant `-MMD -MP` et rappellent pourquoi.

**Le tracé est vectoriel, sans processeur graphique.** Les formes arrondies ne sont plus des tables
d'entames mais **un champ de distance signée** : une seule formule donne la distance d'un point à un
rectangle arrondi, et cette distance donne la couverture du pixel qui chevauche le bord — donc
l'antialiasing. C'est ce qui retire l'escalier d'une courbe, ce qu'aucun agrandissement ne pouvait faire.
Circle porte bien un OpenGL ES dans `addon/vc4`, mais c'est le blob VideoCore IV — Pi 1 à 3 seulement, une
dépendance énorme, et aucune aide pour le texte. Le vectoriel utile ici est le calcul, pas le matériel.

Les icônes du chrome suivent : **peintes depuis le rectangle qu'on leur donne** et non stockées, donc nettes
à n'importe quelle échelle. Effet de bord bienvenu, le chrome ne cite alors plus aucun système, ce que
§7.12 demandait — seuls les dossiers Système restent des relevés, et c'est justement là que l'époque doit
se voir.

**État au 2026-09-01 — la typographie et l'indépendance à la résolution.** Les Helvetica bitmap X11 sont
vendorisées et lues par `gen-font.py`, six tailles en deux graisses ; l'interface se trace à la résolution de
l'écran, échelle 36/16 mesurée sur un 1920×1080. Vérifié aussi : la capture QEMU en 640×480 reste identique
au rendu sur l'hôte, octet pour octet, malgré la bascule de la surface en 32 bits.

**Le piège des métriques s'est refermé quatre fois**, et c'est la leçon la plus utile de l'étape : la coche
et le triangle des menus locaux étaient à coordonnées fixes et flottaient dans une case deux fois plus
grande à l'échelle 2 ; le pouce d'ascenseur avait un minimum de 16 pixels ; l'anneau de focus prenait le
rayon du bouton partout, donc il ne suivait ni une déroulante ni une case ; et l'épaisseur des traits était
un `1` en dur, si bien qu'à l'échelle 2 un contour de bouton restait un cheveu à l'intérieur d'un anneau de
quatre pixels. **Une valeur en dur là où une métrique était due**, chaque fois. §7.12 l'annonçait, et le
savoir n'a pas suffi : c'est l'écran de spécimen qui l'a rendu visible, à chaque fois. D'où la métrique
`nStroke`, qui porte désormais l'épaisseur de tout le chrome.

Deux corrections d'aspect qui viennent d'un œil et non d'un raisonnement : la case cochée portait une coche
crochue, qui est la marque d'un formulaire administratif — un Macintosh y mettait **une croix** ; et le
bouton radio n'avait pas assez de blanc entre l'anneau et le point, ce qui le faisait lire comme une tache.

**État au 2026-08-31 — le système de composants tient debout et se regarde.** `src/firmware/` contient la
surface et les primitives, la fonte générée, le thème, les composants et l'écran de spécimen ; aucun type
Circle n'y apparaît. Le noyau de spécimen (`src/firmware/circle/`) lie Circle et ce code, rien d'autre : ni
cœur Basilisk, ni carte, ni émulateur. Il pèse 459 Ko, se construit en quelques secondes et ne peut donc
rien abîmer.

Le résultat qui compte le plus n'est pas l'image : `scripts/specimen.sh` compare la capture QEMU au rendu
produit sur l'hôte par `tests/host`, et les deux sont **identiques octet pour octet** sur 921 600 octets.
Le rendu sur l'hôte est donc un substitut fidèle, et itérer sur le dessin ne demande plus ni noyau ni
émulateur — ce qui était l'argument pour faire prendre une surface aux primitives plutôt qu'un
`C2DGraphics`, et il se vérifie.

Trois choses apprises en écrivant, qui valent d'être notées ailleurs que dans le code :

- **l'inversion vidéo a un domaine, et c'est le rectangle.** Inverser par-dessus un bouton arrondi retourne
  ses quatre coins et les fait ressortir en encoches blanches. Un bouton enfoncé se peint donc, tandis
  qu'une ligne de liste s'inverse — et l'inverser *après* son contenu est ce qui permet à une seule liste
  de servir les systèmes, les réglages et l'arborescence de la phase 17 : ce que la ligne contient est
  sélectionné par la même règle, quoi que ce soit ;
- **un cercle se dessine en testant les pixels par leur centre.** La première version testait le coin et
  produisait des angles encochés à petit rayon, ce qui se voit immédiatement sur un bouton de 22 pixels ;
- **le décodage UTF-8 appartient à la primitive de texte.** Les sources restent en UTF-8, lisibles par les
  éditeurs et les diffs, et la fonte reste en ISO-8859-1. Écrire des littéraux Latin-1 aurait marché et
  aurait aussi ouvert un piège discret : `"\xE9"` suivi d'une lettre qui se trouve être un chiffre
  hexadécimal avale cette lettre.

**Ordre d'exécution de ce qui reste.** Le socle est là — surface, primitives, fonte, thème, composants,
spécimen — et rien de tout cela n'a encore été branché sur la machine. L'ordre ci-dessous place le risque
en premier et le contenu en dernier, chaque étape se vérifiant seule.

**16a — Le firmware s'ouvre, et rend la main.** L'insérer dans `CKernel::Run()` avant `StartMacintosh()` :
à ce point la carte est montée, les préférences lues, la RAM Mac allouée et l'USB initialisé — c'est
`Initialize()` qui s'en charge, `InputInit()` ne venant que plus tard. Réclamer le framebuffer, peindre le
fond gris deux secondes, le relâcher, laisser le Mac démarrer.

  **C'est l'étape risquée, d'où sa place.** `VideoInit()` construit son propre `CBcmFrameBuffer` ; deux
  instances successives par le même canal mailbox n'ont jamais été essayées ici. Trois issues possibles —
  la seconde allocation réussit et tout va bien, elle échoue et il faut partager une seule instance, ou
  elle réussit en laissant la première fuir. À trancher par l'essai avant d'écrire une ligne de plus, sous
  QEMU puis sur matériel. Vérifié quand l'écran reste gris deux secondes puis affiche le Mac, et que
  `scripts/run-test.sh` reste vert.

  **Fait le 2026-09-01.** L'essai a répondu net : réclamer, relâcher et réclamer à nouveau fonctionne —
  même adresse, même taille, tampon vivant à chaque fois, et `VideoInit()` crée le sien ensuite sans se
  plaindre. Sous QEMU seulement ; le mailbox appartient au firmware du Pi et non à nous, donc à reconfirmer
  sur matériel. Mesuré au démarrage : à t=3 s l'écran est uniformément `#BCBCBC`, à t=48 s le Finder est là
  avec ses volumes, et `run-test.sh` rend son verdict habituel — 57 octets écrits, drapeau `0100` et `fsck`
  propre après réparation. Le noyau passe de 1,8 à 2,1 Mo, sous le plafond de 4.

**16b — Le clavier et la souris, avant le Mac.** Le firmware pose ses propres gestionnaires HID bruts. Le
relais est gratuit : `RegisterKeyStatusHandlerRaw` n'en garde qu'un, et `InputInit()` reprend la main
ensuite sans qu'on ait rien à défaire. `Option` seule ouvre le sélecteur, Cmd-Option-P-R oublie la PRAM.
Vérifié en journalisant les touches reçues pendant la fenêtre.

  **Fait le 2026-09-01.** Le clavier répond pendant la fenêtre sans qu'on ait rien à interroger — les
  rapports arrivent par URB — et le relais est bien gratuit. Deux choses apprises :

  - **rendre le clavier par `RegisterKeyStatusHandlerRaw (0)` bloque le démarrage.** Ce n'est pas un
    détachement mais un changement de mode : le rapport retombe alors dans le mode cuit
    (`usbkeyboard.cpp:200`) et part vers `CKeyboardBehaviour`. Le noyau s'arrêtait net après le firmware,
    sans une ligne de journal. Il n'y a rien à rendre : `InputInit()` remplace le gestionnaire, c'est tout
    le relais ;
  - **tout se verrouille sur la durée de la fenêtre**, modificateurs compris. Un rapport arrive à chaque
    changement, relâchement inclus, donc les quatre touches de Cmd-Option-P-R ne sont presque jamais
    présentes dans le même : la première version exigeait cette coïncidence et manquait la combinaison
    qu'elle guettait. Verrouiller correspond d'ailleurs à ce que faisait un Macintosh, qui échantillonnait
    l'état du clavier pendant la fenêtre.

  Mesuré : `Option` seule est reconnue et le Mac démarre ; Cmd-Option-P-R efface `/BasiliskII_XPRAM` et le
  Mac démarre puis propose de reconstruire son bureau, ce qui est la conséquence normale d'une PRAM oubliée
  et la meilleure preuve que le zap a porté ; sans touche, aucun message ; `run-test.sh` reste vert.

  La souris attendra **16c** : sans écran où cliquer elle ne se teste pas, et le curseur appartient à la
  boucle qui s'en sert.

**16c — La boucle d'écran.** Un écran est un tableau statique de composants et une boucle d'événements —
le `ModalDialog` du Dialog Manager. Focus et son parcours (Tab, Maj-Tab, flèches en liste, Espace, Retour,
Échap), `WidgetHit` enfin appelé, état enfoncé pendant le clic. Vérifiable sur l'hôte en injectant des
événements de synthèse : c'est le premier morceau du firmware qui se teste sans écran.

  **Fait le 2026-09-02.** `okapia_event.h` pose l'événement — touche logique, modificateurs, pointeur —
  et `okapia_screen.{h,cpp}` la boucle : focus et son parcours, `WidgetHit`, enfoncement suivi comme
  `TrackControl` (sortir du contrôle le relâche, y revenir le reprend, relâcher dehors annule).
  `tests/host/check_screen.cpp` l'exerce par événements de synthèse, 33 mesures, sans écran ni clavier.

  Quatre choses tranchées en l'écrivant :

  - **Espace suit le focus, Retour suit le bouton par défaut.** Les faire converger rend l'un des deux
    imprévisible ; les séparer est ce que promet l'anneau autour du bouton par défaut ;
  - **les flèches appartiennent à la liste.** Ailleurs elles ne font rien, sciemment : en faire un second
    parcours de focus donnerait un écran dont le comportement dépend de l'endroit où le focus se trouve
    déjà. Le focus se pose sur le cadre de liste, jamais sur ses lignes, sinon Tab traverse les volumes ;
  - **une ligne appartient à la liste qui la contient**, géométriquement et non par un champ. La règle se
    voit à l'écran, donc elle ne peut pas se désynchroniser de ce qui est dessiné ;
  - **`nGroup` sur les radios.** Effacer tous les radios de l'écran est la version évidente et elle est
    fausse le jour où un écran pose deux questions — le sélecteur en posera.

  `ScreenInit` **suit le focus déclaré** par l'écran s'il y en a un, et n'efface rien : ranger le tableau
  est le travail de Tab. C'est ce qui garde le spécimen vivant identique au rendu de `tests/host`, et le
  dialogue de 16i ouvrira sur son champ.

  Côté Circle, `circle/okapia_input.{h,cpp}` traduit les rapports USB en événements — les identifiants
  d'usage s'arrêtent là — et le noyau spécimen répond maintenant : Tab, Espace, Retour, flèches,
  Gauche/Droite pour la page. La fenêtre de deux secondes passe par le même pont, donc elle a aussi la
  souris. Le pointeur est dessiné par le firmware (`GfxCursorShow`, sauvegarde de ce qu'il recouvre) et
  n'apparaît qu'au premier mouvement : un menu que personne n'a touché n'a rien à pointer, et cela garde
  la capture QEMU identique au pixel près au rendu de l'hôte.

  Deux pièges de Circle, tous deux inscrits dans AGENTS.md : la **souris ne se réclame qu'une fois**
  (`mouse.cpp:85` assène une assertion sur la seconde inscription et n'offre aucun retrait), donc le pont
  garde l'inscription et transmet — `FwInputPassMouseTo()` est ce qu'appelle `InputInit()` ; et un
  **contrôleur USB construit en membre** du noyau tourne avant la série, donc un noyau muet sans un mot
  d'explication.

**16d — Le modèle de boîtes.** Lignes et colonnes, mesure du texte, et **troncature d'une chaîne à son
contrôle** — la limite connue, relevée par le mesureur. Les ordonnées du spécimen ne sont plus posées à la
main, et sa pagination cesse d'être une décision. Les contrôles gagnent « aucun texte hors de son
contrôle ».

  **Fait le 2026-09-02.** `okapia_layout.{h,cpp}` : un rectangle qu'on dépense. Une bande est prise sur
  un bord, ce qui reste est ce qu'il reste à placer. Pas une coordonnée dans `okapia_specimen.cpp`, et le
  bas du dialogue est réclamé **avant** que le milieu soit rempli — c'est précisément ce qui manquait aux
  deux tours de « le contenu est dans la bordure », qui n'ont jamais touché que la droite et le bas parce
  que la mise en page descendait du haut et que rien ne remontait de l'autre côté.

  La troncature vit dans `GfxTextBox`, par où passe **chaque** libellé du chrome : la règle « aucun texte
  hors de son contrôle » est donc vraie par construction et non parce que chaque partie y pense. Ce qui ne
  tient pas est coupé et terminé par des points de suspension, comme `TruncString` — un libellé coupé en
  plein milieu d'une lettre se lit comme un défaut de tracé, celui qui finit par trois points dit qu'il y
  a une suite. Un libellé coupé est tracé à gauche quel que soit l'alignement demandé : le centrer laisse
  un blanc à gauche et l'ellipse loin du bord droit, ce qui se lit comme une erreur deux fois.

  La passe l'a payé tout de suite : la case et le radio passaient **la largeur entière** du contrôle pour
  leur étiquette, qui dépassait donc de la taille de la boîte plus l'espace — 19 px, invisibles jusqu'à ce
  que deux cases voisines se touchent. `check_geometry` a maintenant une mesure pour ça, et elle a été
  vérifiée en réintroduisant le défaut.

  Deux règles du modèle valent d'être retenues : **une ligne réserve de chaque côté d'un contrôle ce que
  son état dessine hors de lui**, sinon une déroulante focalisée en fin de ligne met son anneau dans la
  marge ; et **`RowRest` ne rend pas tout ce qui reste**, il en retire d'abord ce que le contrôle porte —
  la version évidente est exactement le bug précédent.

  **Le spécimen ne redevient pas une page, et c'est mesuré, pas subi.** Les sections sont enchaînées et
  une page finit où la place finit ; une section qui déborde est **reportée entière**, jamais rabotée — le
  rabotage était la faute des quatre tours de bousculade, et c'est la page qui était en cause, pas le
  contenu. Le compte est donc demandé (`SpecimenPageCount`) et non déclaré : il vaut 2 à 640x480, et il ne
  change pas avec l'écran puisque tout le dessin suit l'échelle — un écran plus grand achète une interface
  plus grande, pas davantage d'interface. La troncature se démontre là où elle arrivera vraiment, sur un
  nom de volume et non dans une section pour elle seule.

**16e — Les composants qui manquent.** Cadre d'alerte, étiquette avec repli à la ligne, liste qui défile
pour de vrai, champ éditable avec curseur et retour arrière. Chacun entre dans l'écran de spécimen le jour
où il existe, sinon personne ne le regarde.

  **Fait le 2026-09-02.** Les quatre composants, chacun dans le spécimen le jour où il existe.

  - **Le paragraphe.** `GfxTextWrap` coupe entre les mots. Mesurer et tracer parcourent **la même boucle**,
    la surface en moins pour l'une des deux : deux boucles qui doivent tomber d'accord sur l'endroit d'une
    coupure sont deux boucles qui finiront par diverger, et le symptôme serait une alerte dont la dernière
    ligne sort de la boîte réservée pour elle. Un mot plus large que la boîte est coupé là où il déborde —
    un volume nommé sans espace n'est pas une raison de dessiner dehors.
  - **L'alerte.** Le même cadre, le filet extérieur doublé. Ce qui interrompt est plus lourd, et c'est tout :
    avec deux couleurs, le poids est le seul registre disponible pour dire « celle-ci n'est pas ordinaire ».
    Le triangle et le point d'exclamation sont **calculés** comme les autres marques du chrome — un contour
    obtenu par différence de deux triangles pleins, ce qui lui donne une épaisseur régulière par
    construction là où trois traits épais qui se rejoignent demanderaient beaucoup d'arithmétique.
  - **La liste qui défile.** Les lignes **cessent d'être des composants** : une liste qui défile ne peut pas
    être un tableau de lignes, puisque chaque défilement voudrait dire les reconstruire et que l'écran
    au-dessus devrait savoir quand. Le List Manager gardait ses cellules pour la même raison. La liste porte
    donc ses articles, la première ligne visible et l'article choisi ; elle dessine son propre ascenseur
    quand il en faut un, dont le curseur occupe **la part de la piste que la vue occupe du tout** — un
    curseur dimensionné sur un nombre de crans ne dit rien de ce qui reste hors de vue, qui est la seule
    chose qu'un ascenseur soit lu pour.
  - **Le champ éditable.** Un champ qui a des octets à lui est modifiable, un champ qui n'en a pas est en
    lecture seule — comme tout le reste. Le curseur est un décalage en octets et les flèches enjambent le
    caractère entier : un curseur garé au milieu d'un « é » le couperait en deux à la frappe suivante, et
    ce qui en sortirait ne serait plus du texte. Plein, il refuse au lieu de tronquer.

  **Et le piège de dépendances a mordu une seconde fois, autrement.** `Rules.mk:271` calcule `DEPS` et ne
  l'inclut jamais, et la règle `%.o: %.cpp` de Circle n'a pas de `-MMD` : un objet compilé par elle ne suit
  aucun en-tête. Ajouter `nChar` à `TEvent` a donc laissé le pont d'entrée lire `nKey` au mauvais décalage,
  et la flèche Bas est arrivée en Échap. Les trois Makefile compilent maintenant avec `-MMD -MP` et
  relisent les `.d` ; celui du spécimen passe même ses propres sources par `obj/` pour cela.

  **Complété le 2026-09-02.** La fenêtre du spécimen s'ouvrait en timbre-poste : la façade cocoa de QEMU
  dimensionne sa fenêtre en points, un par pixel invité, et ne la redimensionne jamais ensuite —
  `run-live.sh` avait tranché la même chose pour l'émulateur, en 1280x960, qui est à la fois confortable et
  exactement le double de 640x480, donc une échelle entière de 2 plutôt qu'une fraction qui fait scintiller
  les courbes.

  En le corrigeant, la comparaison capture QEMU / rendu hôte a divergé, et pour une bonne raison : le rendu
  de l'hôte ne faisait pas tourner la boucle, donc il ne montrait pas où le focus se pose. Les deux le font
  maintenant — et la mesure a immédiatement trouvé un vrai défaut : **un contrôle doit réserver la place de
  l'anneau de focus qu'il peut recevoir, pas seulement celle de l'état dans lequel il est tracé.** La mise
  en page tourne avant la boucle et ne peut pas savoir qui sera focalisé ; l'icône de réglages du pied de
  page, posée en `StateNormal`, mettait son anneau un pixel dans la marge dès que le focus s'y posait. La
  règle est inscrite dans `okapia_layout.h`, et le contrôle géométrique la garde.

  **Repris le 2026-09-02 après un essai à la main**, qui a trouvé quatre choses que ni les mesures ni les
  captures ne pouvaient voir :

  - **tout l'écran était repeint à chaque événement**, d'où un clignotement — le fond redescendait avant
    les contrôles — et un coût tel sous une fenêtre que les rapports s'empilaient derrière et que le
    pointeur s'arrêtait en chemin. L'écran tient maintenant la liste de ce qui a changé et ne redessine
    que cela, en remettant d'abord le fond sous chaque contrôle ; il ne redemande le tout que pour le
    premier tracé et le changement de page. **C'est le contrôle « aucun contrôle n'en chevauche un
    autre » qui rend ce redessin isolé légitime** — les deux vont ensemble ;
  - **la saisie ne marchait pas** parce que rien ne produisait de caractère. Le pont passe désormais par
    `CKeyMap` de Circle, donc par la disposition dont la machine est configurée : un clavier français
    tape ce qui est écrit dessus. Les touches de navigation continuent de venir de l'identifiant d'usage
    USB, qui est physique — Tab est Tab où que soient passées les lettres ;
  - **l'ascenseur ne répondait pas.** Cliquer sous ou sur le curseur avance ou recule d'une page, tirer le
    curseur défile. Et le curseur n'est plus calculé à deux endroits : le thème le dessine et la boucle le
    teste depuis `ThemeScrollThumb`, faute de quoi la main atterrit à côté de ce qu'elle voit ;
  - **le cadre d'alerte** doublait le mauvais filet. La paire ne fait un cadre que parce que l'un des deux
    mène ; épaissir l'extérieur retournait ce rapport. C'est l'intérieur qui est doublé, donc la même
    construction que le dialogue, en plus appuyé. Le triangle est plus fin, son intérieur est décalé
    **perpendiculairement à chaque côté** et non sur chaque axe — un sommet aussi aigu est bien plus épais
    le long de sa bissectrice —, et le point d'exclamation est le vrai glyphe de la fonte plutôt qu'une
    barre et un carré à re-régler à chaque taille.

  Au passage, la mesure a rattrapé une barre d'exclamation de hauteur négative : écrite comme « ce qui
  reste après le point et l'écart », elle devenait un rectangle non signé énorme qui remplissait toute une
  colonne du dialogue, à une taille sur trois seulement. Les parts d'une répartition ne se calculent pas
  par soustraction.

  **Deuxième passage à la main, 2026-09-02.** Quatre choses de plus, dont deux que la première correction
  n'avait fait qu'effleurer :

  - **le clignotement ne venait pas du volume repeint mais de l'endroit.** On dessinait dans le tampon
    visible, donc le fond redescendait sous les yeux avant le contrôle posé dessus, et à soixante
    rafraîchissements par seconde cela se voit. Tout est maintenant tracé dans une **surface d'ombre** et
    seule la partie changée est recopiée d'un coup. Une seule allocation, au démarrage ;
  - **la barre d'espace et le retour arrière manquaient.** L'usage 0x2A n'était pas dans la table, et
    Circle range l'espace parmi ses touches spéciales, à 0x100, donc le filtre « caractère imprimable » le
    jetait : le champ prenait toutes les lettres et refusait la seule touche entre les mots. Un clic dans
    un champ y pose aussi le curseur, la mesure du texte et l'encart du thème venant d'un seul endroit ;
  - **la déroulante n'avait qu'une apparence.** Elle ouvre un vrai menu, posé de façon que le choix courant
    tombe sur le contrôle — ce qui est déjà sous la main ne demande aucun mouvement — et il est modal : rien
    dessous ne répond tant qu'il est ouvert. Un menu est **une liste**, donc c'en est une, et elle ne vit
    pas dans le tableau de l'écran : rien ne se met en page autour de ce qui n'est là qu'une fraction du
    temps. En se refermant il redemande tout l'écran, étant la seule chose ici qui en recouvre ;
  - **le cadre d'alerte** reprend exactement les deux épaisseurs du dialogue. Deux tentatives pour le
    rendre plus pressant en épaississant l'un ou l'autre filet ont échoué pour la même raison : ces deux
    épaisseurs sont une constante du système, pas un réglage. Ce qui distingue une alerte, c'est sa marque
    et ce qu'elle dit ; un cadre presque pareil se lit comme une erreur, pas comme une emphase.

  **Troisième passage à la main.** Le blocage périodique n'était pas la mise en page : `CActLED::Blink()`
  n'est pas un signal au voyant mais **deux attentes bloquantes**, 200 ms allumé et 500 ms éteint
  (`actled.cpp:95`). Un appel par seconde dans la boucle l'arrêtait sept dixièmes de chaque seconde — ce
  qui se lit exactement comme un émulateur à bout de souffle, et c'était une ligne de décoration. Inscrit
  dans AGENTS.md.

  Avec elle : le paragraphe est **centré verticalement** dans sa bande, faute de quoi il pendait du
  plafond de l'alerte, dont la hauteur est fixée par la marque et non par le texte ; un **article de menu**
  a sa propre métrique, plus serrée que la ligne d'une liste qui doit loger une icône de dossier ; le
  **curseur de saisie clignote** à la demi-seconde, l'anneau de focus restant allumé — un anneau qui
  clignoterait donnerait l'impression que le champ perd le focus ; et le **pointeur devient une barre en I**
  au-dessus d'un champ modifiable, avec son propre point de visée, l'écran étant seul à savoir ce qu'il y
  a dessous.

  **Finitions.** Le pointeur accélère au-delà de deux points de déplacement — précis quand la main va
  lentement, rapide quand elle balaie, ce que tout système fait depuis que la souris existe ; mesuré, un
  delta de 100 déplace de 195 pixels. Le pied de page réserve son air **par le bas**, la barre au-dessus
  des boutons ayant été construite vers le haut : pris par le haut, l'écart atterrissait ailleurs et le
  filet touchait les boutons.

  La marque d'alimentation a demandé trois corrections successives, toutes de la même famille — **on ne
  suppose pas où l'encre tombe, on le mesure** :

  - l'encoche était **rectangulaire** alors que le trait doit être coupé **le long d'un rayon**. Une coupe
    verticale traverse l'arc en biais, donc sa face est plus longue que l'épaisseur du trait et aucun
    capuchon rond ne peut la couvrir : il restait un ergot au-delà de chaque bout ;
  - les capuchons étaient posés au **centre** de l'encoche alors que l'arc est coupé à son **bord**, une
    demi-épaisseur plus loin ;
  - et le rayon de la ligne médiane valait `(largeur - épaisseur) / 2`, alors que `GfxCircleFrame` travaille
    depuis un champ de distance pris au centre du rectangle et que les centres de pixels sont un demi-pixel
    plus loin : l'encre tombe à `(largeur - épaisseur) / 2 + 1/2`, **vérifié de seize à quatre-vingt-seize
    pixels** en scrutant le tracé. Sans ce demi-pixel, les tailles où la division tronquait aussi
    décalaient le capuchon d'un pixel entier — d'où des petites tailles fautives et des grandes correctes,
    ce qui envoyait chercher au mauvais endroit.

**16f — Les traductions.** Une table de chaînes, anglais et français, depuis `boot-menu/strings.tsv` ;
langue persistée dans les préférences. À faire avant le sélecteur, pas après : c'est ce qui garantit que la
mise en page ne s'est pas calée sur la longueur des libellés français.

  **Fait le 2026-09-02.** `assets/strings.tsv` porte chaque libellé dans chaque langue, et
  `scripts/gen-strings.py` en tire **et** les tables **et** l'énumération `TStringId` — la même habitude
  que la table clavier et les fontes. Une clé renommée ou retirée casse donc la compilation au lieu de
  laisser une étiquette vide sur un écran que personne n'a ouvert ce jour-là. Les tables des deux langues
  sont compilées ensemble : deux kilo-octets, et plus aucune question sur le moment de charger quoi.

  **Et c'est là que se joue la raison de le faire avant le sélecteur** : `check_geometry` mesure désormais
  chaque page, à chaque taille, **dans chaque langue**. Une mise en page calée sur une langue puis traduite
  est une mise en page qui se disloque, et le français est plus long que l'anglais presque partout. Douze
  mesures de débordement au lieu de six, toutes vertes — les boutons se dimensionnant depuis leur propre
  libellé, « Démarrer » et « Start » ne demandent pas la même largeur et la page se recompose.

  L'anglais est le défaut : c'est la langue du code, et celle dans laquelle les mises en page sont lues en
  premier. La carte tranche par la préférence `language`, un code à deux lettres ; un code inconnu répond
  la première langue plutôt que d'échouer, pour qu'une carte écrite par une version ultérieure démarre
  quand même. Dans le spécimen, `L` fait le tour des langues — ce n'est pas un contrôle du produit, le vrai
  sera une déroulante dans les réglages, mais c'est le seul moyen de voir à la main ce que la mesure
  vérifie déjà.

  **La marque d'alerte, reprise sur le panneau System 6/7.** Trait fin, **angles arrondis**, et un point
  d'exclamation qui remplit vraiment le triangle — un contour épais avec une petite marque dedans se lit
  comme une forme, pas comme un avertissement. Le triangle est tracé par champ de distance
  (`GfxTriangleFrame`), comme les autres formes arrondies : l'arrondi vient d'un rayon soustrait à la
  distance, et l'intérieur du contour d'un décalage **perpendiculaire à chaque côté** obtenu par une
  homothétie de centre l'**incentre** — depuis le centre de gravité, les trois côtés rentreraient de trois
  quantités différentes.

  Le point d'exclamation est le glyphe de la fonte, et **la place est décidée avant la fonte** : la marque
  est posée, puis on prend la plus grande fonte qui tient à cet endroit. L'ordre inverse — choisir la
  fonte puis la caser où il reste de la place — a donné successivement une marque collée à la base, puis
  une marque plaquée contre la pente, parce que la position était le reste et non la décision.

  Deux mesures valent d'être retenues :

  - **la place se mesure sur l'encre, pas sur la chasse.** Deux ou trois pixels d'approche sur un glyphe
    de cinq, c'est un barreau d'échelle de différence — `GfxTextInkWidth` ;
  - **le centre d'un triangle n'est pas sa mi-hauteur.** Toute la masse est en bas, donc une marque posée
    au centre géométrique flotte dans la partie étroite avec un champ noir dessous et se lit **haut**. Elle
    est posée aux trois cinquièmes : deux cinquièmes de hauteur au-dessus, trois en dessous. Poser plus
    bas laisse aussi passer une fonte plus grande, la largeur y étant plus généreuse.

  Un rappel au passage : le fût touchait le côté gauche à la taille qu'une alerte utilise vraiment, et
  deux traits qui se rejoignent se lisent exactement comme une moitié de marque mangée.

  Limite connue : l'échelle de fontes plafonne à une capitale de 25 pixels, donc au-delà d'un triangle
  d'environ 70 pixels la marque cesse de grandir. L'icône de l'alerte fait deux interlignes, ce qui la
  garde en deçà à toutes les échelles utilisées — et c'est aussi la proportion du panneau d'époque.

**16g — Le sélecteur.** Le premier vrai écran. `HfsInventory()` et `HfsSystemVersion()` alimentent les
lignes — la phase 15bis les a déjà livrés —, colonne radio pour le défaut, cases lecture seule et montage,
et écriture dans `BasiliskII_Prefs` par `SavePrefs()`.

**16h — Réglages, informations, arrêt, oubli de la PRAM.** Le reste des écrans, une fois la mécanique
éprouvée par le sélecteur.

**16i — Le dialogue de réparation.** En dernier parce qu'il écrit dans le volume de l'utilisateur, et qu'il
mérite que tout le reste soit sûr avant lui.

- [ ] **dialogue de réparation du volume** : aujourd'hui `HfsRepair` scavenge en silence au démarrage.
      C'est ce qu'il faut pour un appareil, mais l'utilisateur doit pouvoir le voir et le refuser —
      « le volume n'a pas été démonté proprement, réparer ? ». C'est la première vraie raison d'être de
      cette interface, et le seul endroit du système qui écrit dans le volume de l'utilisateur sans qu'il
      l'ait demandé. Trois états et non deux : réparer sans demander, demander, ne jamais réparer —
      par un second booléen `hfsrepairask`, sans changer le type de `hfsrepair` (`prefs_circle.cpp:57`),
      avec un décompte qui répare à l'expiration, faute de quoi `scripts/run-test.sh` s'arrête dessus

### Phase 17 — Provisionnement

- [ ] création du disque persistant vide au premier démarrage
- [ ] catalogue en ligne arborescent (System 6 / 7 / Mac OS 8 …), HTTPS via mbed TLS
- [ ] téléchargement par blocs avec reprise, décompression `zlib`, écriture sur SD
- [ ] adresse du catalogue configurable, fonction désactivable, rien de réhébergé

### Phase 18 — Expérience d'appliance

- [ ] mise sous tension → écran noir → Happy Mac → Finder ; aucun journal sur HDMI, tout sur UART
- [ ] fichier de configuration sur la SD (format de préférences Basilisk, §8)
- [ ] arrêt propre soigné

---

## 11. Jalons

| | Livrable |
|---|---|
| M0 | poste de travail local complet, dépôt qui compile |
| M1 | Circle sous QEMU : framebuffer, entrées, SD, GDB |
| M2 | le même binaire sur Pi 3/4 |
| M3 | ROM chargée, 68040 en exécution |
| M4 | disque Mac visible |
| M5 | premiers pixels Mac |
| M6 | **Finder utilisable, sous QEMU et sur matériel** |
| M7 | dossier partagé opérationnel |
| M8 | horloge, PRAM, arrêt propre |
| M9 | mesures de référence établies |
| M10 | vidéo optimisée, sans déchirement |
| M11 | réseau et audio |
| M12 | séquence Macintosh : carillon, Happy Mac, Sad Mac et ses codes |
| M13 | firmware Okapia : configuration à la souris, sans fichier texte |
| M14 | provisionnement : Okapia utilisable sans rien préparer |
| M15 | appliance : mise sous tension → Mac OS |

Hors périmètre initial : JIT ARM64, NVMe, System 6, profils de machines multiples.

---

## 12. Tests

**Sur l'hôte**, tout ce qui peut l'être : conversions de pixels, palette, endianness, correspondance des
touches, analyse de la configuration, découpage en blocs disque, aides au plan mémoire.

**Sous QEMU**, automatisé : démarrage, présence des messages attendus, initialisation de la ROM,
framebuffer, disque. Détecter les messages série attendus, avec expiration si le démarrage se bloque,
et produire `kernel8.img` comme artefact.

**Sur matériel**, liste à cocher : HDMI, SD, USB, audio, Ethernet, température, stabilité longue durée,
coupure brutale.

**Emprunt utile** : le corpus de tests de `rcarmo/macemu-jit` (`jit-test/`, `BasiliskII/qa/`, harnais ROM)
est réutilisable **sans son JIT**, pour valider notre interpréteur.

**Aucune ROM Apple dans la CI publique.** La CI se limite à : compilation, démarrage de Circle, lecture d'un
fichier depuis la FAT. Une fausse ROM ne passerait pas la phase 4 de toute façon — Basilisk vérifie taille et
somme de contrôle.

---

## 13. Débogage

**QEMU** : GDB, points d'arrêt, pas à pas, inspection mémoire, UART. C'est l'environnement principal pour
les bugs bas niveau reproductibles.

**Matériel** : UART, assertions, vidages sur plantage, tampon circulaire de traces, compteur de dernière
étape franchie, chien de garde optionnel. Le `rpi_stub` de Circle ne couvre que les Pi 2/3 ; sur Pi 5 le
débogage passe par SWD (Debug Probe). Trois mécanismes distincts, à ne pas confondre.

Niveaux de journalisation toujours disponibles : `QUIET NORMAL DEBUG TRACE`.

---

## 14. Documentation à maintenir

`architecture.md` (flux de démarrage, couches, responsabilités) · `memory-map.md` (RAM Mac, ROM,
framebuffer, tas Circle, `MEMBaseDiff`) · `video.md` (modes, compositeur, palette, mise à l'échelle) ·
`porting-notes.md` (chaque incompatibilité rencontrée, chaque fichier adapté et sa provenance) ·
`hardware.md` (différences Pi 3 / 4 / 5, réserve USB) · `benchmarks.md`.

---

## 15. Risques

| | Risque | Parade |
|---|---|---|
| R1 | **QEMU ne teste pas le chemin Pi 5** — et valide même un mode vidéo qui n'existe pas dessus | passer sur matériel dès la phase 2 ; le compositeur rend les trois modèles identiques |
| R2 | **Réserve USB sur Pi < 4** : le réseau affame clavier et souris | documenté ; Pi 4 pour l'usage |
| R3 | Dépendances POSIX cachées | largement levé : circle-stdlib couvre le nécessaire, et V2 donne l'inventaire exact |
| R4 | Endianness et formats de pixels | commencer en 8 bits ; les routines amont gèrent le big-endian ; tests hôte |
| R5 | Corruption disque | vidage toutes les 2 s, arrêt propre, images de référence en lecture seule, sauvegardes |
| R6 | ROM incompatibles | une seule ROM (Q650) tant que le système n'est pas stable |
| R7 | Optimisation prématurée | mesurer d'abord (phase 11) ; S2/S3 et suivi des zones modifiées seulement sur preuve |
| R8 | Fidélité du FPU (`fpu_uae`) | acceptable : peu d'applications d'époque en dépendent finement ; MPFR reste possible plus tard |

---

## 16. Règles de travail

1. Jamais de stub silencieux : tout stub journalise et échoue proprement.
2. Chercher le code en amont **avant** d'en écrire.
3. Tout fichier adapté porte sa provenance exacte en en-tête, et la liste de ses modifications.
4. Patches amont minuscules, documentés dans `patches/macemu/`, proposés en amont s'ils ont un intérêt général.
5. Un commit par fonctionnalité cohérente.
6. Un bug reproductible corrigé s'accompagne d'un test.
7. Corriger avant d'optimiser ; mesurer avant d'optimiser.
8. QEMU doit rester fonctionnel après l'arrivée du matériel.
9. Toute optimisation propre à un modèle a un chemin générique de repli.
10. Documenter les hypothèses sur les caches, la MMU et le DMA.
11. Ne jamais versionner de ROM ni de système Apple.
12. M5Tab n'est pas une vérité : comparer avec macemu amont. Son code n'est pas réutilisable en l'état
    (licence non déclarée).

---

## 17. Première mission

Créer le squelette du dépôt, sans aucun code Macintosh.

- sous-modules `circle-stdlib` et `macemu`, épinglés
- `bootstrap.sh`, `build-qemu.sh`, `run-qemu.sh`, `build-pi.sh`
- un noyau Circle AArch64 minimal : sortie série, framebuffer, mire, clavier, souris, lecture d'un fichier
  sur une image SD
- documentation exacte de la compilation et du lancement **sous macOS**

**Ne pas** intégrer Basilisk, l'audio, le réseau, le JIT, le multicœur ni la vidéo optimisée à ce stade.

Le programme affiche **ce qu'il a réellement obtenu** du firmware (résolution, profondeur, pitch) et ne
prétend jamais avoir le mode demandé.

```bash
./scripts/bootstrap.sh
./scripts/build-qemu.sh
./scripts/run-qemu.sh
```

```text
Macintosh Circle
Circle initialized
Framebuffer 640x480x8 (pitch 640) — requested 640x480x8
USB initialized
SD initialized
Ready
```

---

## 18. Après

**Deuxième moteur : SheepShaver (PowerPC).** C'est l'extension la plus naturelle, puisque la couche
plateforme est déjà partagée en amont (§3.5) : Mac OS 8.5 → 9.0.4, et tout le logiciel PowerPC. Deux
préalables, dans cet ordre : régler le mappage mémoire à adresses fixes sous Circle, puis **mesurer**
l'interpréteur PPC sur la carte. Si le résultat est inutilisable, la question devient celle du backend JIT
AArch64 — c'est-à-dire un projet en soi, à ne pas ouvrir avant que Basilisk ne soit stable et mesuré.

Ensuite : profils de machines multiples, gestionnaire d'amorçage (choix moteur + ROM + disque), Mini vMac
pour System 1 → 7.5 en noir et blanc, accélération QuickDraw ciblée, presse-papiers partagé, AppleTalk, mode
CRT optionnel, image SD prête à flasher, Pi 5 pleinement pris en charge, NVMe, et — seulement si les mesures
le réclament — le **JIT 68k ARM64**.

Sur ce dernier point, l'état des lieux au 2026-08-24, pour ne pas avoir à le refaire :

- le JIT **amont** de Basilisk II est **x86 uniquement** (`CAN_JIT=yes` seulement pour i386 et x86_64,
  `Unix/configure.ac:1651` et `:1660`) ; `uae_cpu/compiler/` ne contient que `codegen_x86.*` ;
- le seul backend 68k→AArch64 existant est dans **`rcarmo/macemu-jit`**, sous `uae_cpu_2026/`. Sa lignée est
  écrite dans les en-têtes : **UAE4ARM (TomB, 2019) → Amiberry → ce fork**, en GPLv2-or-later, donc
  compatible ;
- **correction : sérieuse.** Son `JIT-STATUS.md` revendique les 48 282 encodages 68040 classifiés
  (46 087 générés en natif), un harnais d'équivalence 904/904, et un passage jusqu'au Finder ;
- **performance : non démontrée.** Le même document présente le gain comme un objectif *à atteindre*, et le
  seul chiffre publié dans le dépôt concerne son JIT PowerPC, défavorable (1,889× plus lent que
  l'interpréteur sur un microbenchmark) ;
- **portage** : le delta est de 23 fichiers ajoutés et 17 modifiés (~5 000 lignes) — adopter ce JIT, c'est
  adopter leur cœur CPU. Trois dépendances système à remplacer : `vm_acquire`/`mmap` (facile),
  `mprotect` + gestionnaire `SIGSEGV` + pas-à-pas `BRK` (à réécrire sur les *data aborts* de Circle), et
  `__builtin___clear_cache` (a priori bon en bare-metal). La contrainte `VM_MAP_32BIT` est
  automatiquement satisfaite sous Circle ;
- **préalable absolu** : rendre le tas exécutable (`PXN=1`, §9 V3).

Son corpus de tests (`jit-test/`, `BasiliskII/qa/`, harnais ROM) est en revanche utilisable **sans son
JIT**, pour valider notre interpréteur.

La priorité reste :

> **un Macintosh 68k bare-metal simple, stable, rapide et maintenable.**


---

## Annexe — sources

Vérifications faites le **2026-08-24** (Circle `master` = release 51 de mai 2026 ; macemu `master` du
2026-08-23). Les affirmations techniques de ce document en proviennent ; les reprendre plutôt que les
re-vérifier.

**Code lu directement**

- Circle — `README.md` (tableau de support Pi 5), `doc/qemu.txt`, `doc/multicore.txt`, `doc/memorymap.txt`,
  `doc/stdlib-support.txt`, `doc/issues.txt`, `lib/bcmframebuffer.cpp`, `lib/translationtable64.cpp`
  (`PXN=1`), `lib/memory.cpp:148` (`SetupHighMem`), `include/circle/memory.h`, `include/circle/memorymap.h`,
  `include/circle/netdevice.h`, `include/circle/sched/scheduler.h`, `include/circle/sound/`,
  `include/circle/bcmframebuffer.h`, `CHANGELOG.md`
- macemu — `BasiliskII/src/Unix/configure.ac` (`:1651`/`:1660` JIT x86, `:1691` `uae_cpu_2021`,
  `:1881-1885` FPU MPFR sur ARM), `Unix/main_unix.cpp` (`:677` plafond 1023 Mo, `:731` bloc contigu),
  `include/video.h` (contrat plateforme), `video.cpp:569` (luminance), `CrossPlatform/video_blit.cpp`,
  `Unix/extfs_unix.cpp:79-83` (`.finf`/`.rsrc`) et sa table `e2t_translation[]`, `emul_op.cpp` (écritures
  PRAM, écriture RTC ignorée), `uae_cpu_2021/memory.h:38-54` (`TRY`/`CATCH`), `src/dummy/`, `src/slirp/`,
  `SheepShaver/src/Unix/main_unix.cpp:183-185` (adresses fixes), `SheepShaver/src/kpx_cpu/src/cpu/jit/`
  (pas de backend AArch64), et les 129 liens symboliques de `SheepShaver/` vers `BasiliskII/`
- circle-newlib — `libgloss/circle/` (46 fichiers) : `io.cpp` (`open`…, `opendir`/`readdir` sur FatFs),
  `select.cpp`, `socketio.cpp`, `netdb.cpp`, `stat.cpp`, `mkdir.cpp`, `rename.cpp`, `clock_gettime.cpp`
- circle-stdlib — `README.md`, options du `configure` (`--qemu`, `-r`, `--kernel-max-size`, `--opt-tls`)
- rcarmo/macemu-jit — `JIT-STATUS.md`, `uae_cpu_2026/compiler/`, historique Git
- QEMU — `hw/display/bcm2835_fb.c` (8/16/32 bpp avec palette)
- infinite-mac — `README.md`, `.gitmodules`, `Images/`

**Documentation et web**

- Circle, appendice Raspberry Pi 5 : <https://circle-rpi.readthedocs.io/en/latest/appendices/raspberry-pi-5.html>
  (« *cannot set display resolution from application* »)
- M5Tab-Macintosh, `README.md` : 2-3 MIPS ≈ Quadra 610 sur ESP32-P4 à 400 MHz, 60-90 % d'économie par
  suivi des zones modifiées
- NTP intégré au Mac à partir de **Mac OS 8.5** (octobre 1998) ; auparavant, utilitaires tiers
- Sons de démarrage et de plantage **stockés en ROM** sur les Macintosh, extractibles
- ROM Macintosh 68k : jamais libérées, sous copyright Apple ; System 7.5.3 distribué gratuitement à
  l'époque, aujourd'hui archivé sous mention d'abandonware

**Dépôts**

`kanjitalk755/macemu` · `codeberg.org/larchcone/circle-stdlib` (miroir `smuehlst/circle-stdlib`) ·
`rsta2/circle` · `randyrossi/bmc64` · `amcchord/M5Tab-Macintosh` · `rcarmo/macemu-jit` ·
`BlitterStudio/amiberry` · `mihaip/infinite-mac` · `michalsc/Emu68`
