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
| Configuration | **firmware Okapia** au look System 7, appelable au démarrage — pas seulement un fichier texte |
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

- **le RTC n'existe que sur Pi 5** (connecteur pile dédié) ; sur Pi 3 et 4 l'ordre effectif est NTP puis SD ;
- **Basilisk ignore les écritures dans l'horloge matérielle** : dans `emul_op.cpp`, la branche « écriture »
  des registres RTC n'est pas implémentée. Régler l'heure dans le Mac tient pour la session mais ne survit
  pas au redémarrage. **Patch prévu** : capter cette écriture, la convertir, la persister — quelques lignes,
  bon candidat à une remontée amont.

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
| `5` | Mac IIci | System 7.x, **obligatoire avant 7.5** |
| **`14`** | Quadra 900 | **Mac OS 8.x — retenu**, puisque la cible est 8.1 |

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
qui aurait pu exister** : un utilitaire de configuration à l'esthétique System 7, en noir et blanc, piloté
au clavier et à la souris, qui s'ouvre **avant** que le moindre code Basilisk ne s'exécute.

Ce qu'il remplace : la configuration par fichier texte. Le fichier de préférences reste la source de
vérité — le firmware ne fait que l'éditer, ce qui garde les deux voies cohérentes.

**Ce qu'il permet** : choisir le moteur et le profil de machine, la ROM, le ou les disques, la résolution et
la profondeur de départ, le réseau, l'audio ; provisionner un système (§7.14) ; et afficher les
diagnostics quand quelque chose manque.

**Déclenchement** : une touche maintenue au démarrage — dans l'esprit des combinaisons Mac — et
automatiquement lorsque la configuration est absente ou invalide. Jamais autrement : une machine
correctement configurée démarre droit sur Mac OS.

**Faisabilité** : Circle fournit `C2DGraphics`, des polices bitmap et l'USB HID ; le firmware s'appuie sur
le même `hal_circle` que l'émulateur (§3.5), ce qui est précisément la raison d'être de cette couche. Un
petit jeu de widgets 1 bit — barre de menus, fenêtre, bouton, case à cocher, liste — suffit, de l'ordre de
quelques centaines de lignes.

**Vigilance** : **Chicago est une police Apple sous copyright**. Ne pas l'embarquer. Soit une police bitmap
libre d'aspect voisin, soit une police 8×16 dessinée pour le projet — auquel cas elle devient un actif du
projet, sous GPLv3 comme le reste.

**Le firmware possède le `modelid`.** C'est sa place naturelle : le champ `productKind` est patché au
chargement de la ROM (§7.11), donc avant que Basilisk ne démarre — impossible à changer une fois Mac OS
lancé. L'utilisateur n'a pas à savoir que « Mac OS 8 exige la valeur 14 » : le firmware offre un choix
**« OS invité : System 7 / Mac OS 8 »** par image disque, et traduit.

Portée réelle du problème, à ne pas surestimer : **`14` convient de System 7.5 à Mac OS 8.1**, soit
l'essentiel de la cible. Seuls les System antérieurs à 7.5 imposent `5`. Un défaut à `14` est donc juste
presque toujours.

**Détection automatique — plus tard, et seulement si elle devient bon marché.** Déduire l'OS invité en
lisant l'image disque est possible, à deux niveaux de coût très différents :

- **le nom du volume est facile** : il est en clair dans le *Master Directory Block*, à l'offset 1024 d'une
  partition HFS. Une cinquantaine de lignes, et le sélecteur de démarrage affiche « Macintosh HD » au lieu
  de « disk0.img » ;
- **la version du Système est lourde** : il faut parcourir l'arbre B du catalogue HFS jusqu'au fichier
  `System` du dossier Système béni, ouvrir son fork de ressources et décoder la ressource `'vers'`.
  Plusieurs centaines de lignes pour trancher un cas de bord.

À faire dans cet ordre, jamais l'inverse : le nom de volume a une valeur d'usage immédiate, la détection
de version n'en a qu'une d'élégance.

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
- rien n'est embarqué dans le dépôt : le son vient de **la ROM de l'utilisateur**. Pas de carillon sans ROM.

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
`prefs_unix.cpp` (§8) :

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
- [ ] `prefs_unix.cpp` adapté, configuration lue depuis la SD — **non fait** : c'est `prefs_dummy.cpp`
      qui est lié, et `CKernel::SetDefaultPreferences()` fixe tout en dur. Rien n'est configurable
      sans recompiler (cf. §7.12, le firmware Okapia)
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

- [ ] `extfs_unix.cpp` adapté à FatFs
- [ ] `.finf/` et `.rsrc/` fonctionnels, table des types
- [ ] essai de bout en bout : archive déposée depuis un PC → visible dans le Finder → détendue → application
      qui se lance

### Phase 10 — Horloge, PRAM, arrêt propre

- [ ] `TimerDateTime()` avec la chaîne NTP → RTC → SD, source journalisée
- [ ] patch `xpram_dirty` et écriture différée
- [ ] patch d'écriture d'horloge
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
- [ ] essais : sons système, lecture AIFF, jeux

### Phase 15 — Séquence de démarrage Macintosh

- [ ] analyse de la carte de ressources du dump ROM, extraction des ressources `snd ` et des icônes
- [ ] carillon joué **le plus tôt possible**, avant l'émulateur (chemin audio minimal initialisé tôt)
- [ ] Happy Mac quand tout est en place
- [ ] Sad Mac, carillon de mort et **codes d'erreur documentés** : ROM absente, ROM non reconnue, aucun
      disque amorçable, SD illisible
- [ ] aucun de ces éléments n'est versionné : tout vient de la ROM de l'utilisateur

### Phase 16 — Firmware Okapia

- [ ] `hal_circle` consolidé et utilisable hors émulateur
- [ ] jeu de widgets 1 bit d'aspect System 7 (barre de menus, fenêtre, bouton, case à cocher, liste)
- [ ] police bitmap libre ou dessinée pour le projet — **jamais Chicago**
- [ ] écrans : moteur et profil, ROM, disques, écran, réseau, audio
- [ ] écriture dans le fichier de préférences, qui reste la source de vérité
- [ ] ouverture par touche maintenue au démarrage, et automatiquement si la configuration manque

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
