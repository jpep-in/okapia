# Étude comparative — infinite-mac / Okapia

Deux portages de la même base, faits pour des machines opposées : `mihaip/macemu`
(branche `infinite-mac-kanjitalk755`) porte Basilisk II et SheepShaver vers
WebAssembly, Okapia les porte vers Circle sur Raspberry Pi. Le navigateur et le
bare-metal partagent presque toutes leurs contraintes : **pas de threads utiles,
pas de `SIGSEGV`, pas de `mmap` à adresse fixe, pas de JIT, une seule boucle qui
doit tout faire.** Ce que l'un a dû résoudre, l'autre le rencontre.

Ce document dit ce qu'il faut en prendre, ce qu'il ne faut pas, et dans quel ordre.

## 1. Méthode

`reference/infinite-mac/` (cloné, `scripts/fetch-reference.sh infinite-mac`) et son
sous-module `macemu/`, cloné à part sur la branche du portage. La base commune
avec notre `external/macemu` est le commit `d7c0303`, ce qui donne le portage
entier en un seul diff :

```bash
git -C reference/infinite-mac/macemu diff --stat d7c0303 HEAD
```

**3 131 lignes ajoutées, 225 supprimées, 76 fichiers.** C'est le même ordre de
grandeur que `src/circle/` (5 272 lignes). Les deux portages ont donc la même
taille, ce qui rend la comparaison ligne à ligne possible et honnête.

Rappel : `reference/` est en lecture seule. Rien n'en est copié sans être réécrit
aux conventions d'ici, et la licence est la même (GPLv3 des deux côtés).

## 2. Correspondance couche par couche

| Couche | infinite-mac | Okapia | État |
|---|---|---|---|
| Vidéo 68k | `JS/video_js.cpp` (488 l., **les deux moteurs**) | `video_circle.cpp` (579 l.) | duplication chez nous |
| Vidéo PPC | *le même fichier* | `sheepshaver/video_circle.cpp` (538 l.) | duplication chez nous |
| Audio | `JS/audio_js.cpp`, modèle *pull* | `audio_circle.cpp`, modèle *pull* | convergent |
| Disque | `JS/disk_js.cpp` via `disk_generic` | `sys_unix.cpp` réutilisé tel quel | à prendre |
| Entrées | `JS/input_js.cpp`, drainé dans le fil 68k | `input_circle.cpp`, poussé depuis le cœur 0 | à corriger |
| PRAM | `pram_helpers.h`, `CheckPRAM()` à 1 Hz | `xpram_circle.cpp`, écriture immédiate | **nous sommes devant** |
| Horloge | `PRECISE_TIMING_EMSCRIPTEN`, sondage | IRQ timer Circle, cœur 0 | divergence assumée |
| Couture périodique 68k | `cpu_do_check_ticks()` | `cpu_ticks_circle.cpp:35` (compteur seul) | sous-employée |
| Couture périodique PPC | `CheckTicks()` dans l'interpréteur | **aucune** | manque |
| Mémoire basse PPC | `gZeroPage`/`gKernelData` statiques | idem, généralisé | convergent |
| Encodage noms ExtFS | `mac_encodings.cpp` (MacRoman/MacJapanese) | identité | **manque** |
| Durabilité | overlay OPFS, sans politique de flush | écriture traversante, testée | **nous sommes devant** |

## 3. À prendre — vérifié, chiffré, sans réserve

### 3.1 `Blit_Expand_1_To_32` ignore la palette (bug hérité, actif chez nous)

`video_blit.cpp:426` écrit `-(bit)`, c'est-à-dire `0x00000000` ou `0xFFFFFFFF`,
au lieu de lire `ExpandMap`. Le portage WebAssembly le corrige en deux lignes.

Ce chemin est **vivant chez nous** : `video_circle.cpp:286` choisit `Screen_blit`
pour tous les modes indexés, et `video_blit.cpp:581` y route la profondeur 1 vers
`Blit_Expand_1_To_32`. Nous construisons pourtant la palette à
`video_circle.cpp:313` — elle est simplement jetée. Un Mac réglé en noir et blanc,
ou un System 6, affiche donc du noir et blanc câblé, jamais la palette demandée
(une palette inversée s'affiche à l'endroit).

**À faire** : reprendre le correctif dans `patches/macemu/`, avec le test hôte qui
va avec. C'est le seul point de cette étude qui est un bug de rendu prouvé.

### 3.2 L'anneau clavier n'est pas sûr entre deux cœurs

`adb.cpp:239` protège la souris par `mouse_lock`, que
`main_circle.cpp:185` implémente en `CSpinLock` — correct. Mais `ADBKeyDown()`
(`adb.cpp:302`) écrit `key_buffer[]` puis `key_write_ptr` **sans verrou**, et
`input_circle.cpp:167` l'appelle depuis les gestionnaires USB de Circle, donc
depuis le cœur 0, pendant que l'interpréteur draine l'anneau depuis l'autre cœur.

Sur x86 le modèle mémoire fort rend cet anneau producteur-consommateur juste par
accident. **AArch64 ne donne pas cet ordre** : rien n'empêche la publication de
`key_write_ptr` de devenir visible avant l'octet du code, et le consommateur lit
alors une touche qui n'a pas encore été écrite. C'est rare, silencieux, et cela
ressemblera à « une touche fantôme », pas à un bug mémoire.

infinite-mac ne rencontre pas le problème parce que son producteur ne touche
jamais `adb.cpp` : `input_js.cpp:15` **draine** un tampon partagé depuis le fil
d'émulation, entre `acquireInputLock()` et `releaseInputLock()`.

**À faire** : même forme. `input_circle.cpp` écrit dans une file à un producteur,
et un `InputDrain()` appelé depuis `cpu_do_check_ticks()` (déjà dans le bon fil,
appelé toutes les 65 536 opcodes ≈ 4,5 ms) appelle `ADBKeyDown()`/`ADBMouseMoved()`.
Ce n'est pas une optimisation de latence — notre producteur est déjà asynchrone —
c'est une correction d'ordonnancement mémoire.

### 3.3 Les noms de fichiers du dossier partagé ne sont pas convertis

`extfs_unix.cpp` livre `host_encoding_to_macroman()` et
`macroman_to_host_encoding()` en fonctions identité, et nous réutilisons ce
fichier tel quel (`src/kernel/Makefile:106`). Un fichier `Résumé.txt` sur la carte
FAT arrive donc au Finder en UTF-8 brut, et un fichier créé par le Mac repart en
MacRoman brut. Sur une machine française — le cas d'usage documenté d'Okapia —
c'est visible au premier dossier.

`mac_encodings.cpp` fait la conversion, et surtout **demande à l'invité quel
encodage il utilise** : `GetMacScriptManagerVariable()` (ligne 316) injecte un
talon 68k de dix-huit octets qui appelle `ScriptUtil()` (`0xA8B5`) pour lire
`smMacSysScript` et `smMacRegionCode`, puis `get_encoding_functions()` (ligne 342)
choisit MacRoman ou MacJapanese. La technique est ce qui compte : elle vaut pour
n'importe quel System, dans n'importe quelle langue, sans préférence à régler.

**À faire** : une table MacRoman↔UTF-8 de 128 entrées (le reste est de l'ASCII),
la détection de script en cache — appelée une fois, pas par nom de fichier comme
là-bas —, et **pas de `malloc` par conversion** : un tampon fixe de 256 octets,
la limite d'un nom HFS étant de 31. Testable sur l'hôte, sans carte ni émulateur.

### 3.4 Le média amovible appartient à `disk_generic`, pas à `Sys_*`

`disk_unix.h:43` gagne trois virtuelles — `is_media_present()`, `is_fixed_disk()`,
`eject()` — et `sys_unix.cpp` les appelle au lieu de renvoyer `true` en dur. Deux
corrections viennent avec : `SysGetFileSize()` interroge le disque au lieu d'un
`file_size` figé, et `Sys_open()` ne fixe la taille que si le média est présent.

Nous réutilisons `sys_unix.cpp` sans le modifier, et nous n'avons donc **aucun**
moyen de présenter un CD-ROM, une disquette, ou d'éjecter quoi que ce soit depuis
le firmware. Ce changement l'ouvre sans toucher à la couche système : un
`disk_okapia_factory` en face de `disk_stubs.cpp`, sur le modèle de
`disk_js.cpp:65`, et le firmware peut monter et démonter une image en cours de
session.

Le patch est petit, générique, et remontable chez `kanjitalk755`.

## 4. À prendre, avec adaptation

### 4.1 Le moteur PowerPC n'a aucune couture périodique dans son propre fil

C'est le manque le plus structurel. AGENTS.md fixe la règle : « la PRAM est écrite
au moment où elle change, depuis `VideoInterrupt()` — pas depuis le gestionnaire
de tick, qui tourne au niveau IRQ où bloquer sur la carte SD est interdit ». Côté
68k, `VideoInterrupt()` fournit ce point. **Côté PowerPC, il n'existe pas** :
`rtk proxy grep -rn "CheckTicks" src/circle/sheepshaver/` ne trouve rien.

infinite-mac a créé la couture : `ppc-cpu.cpp:583` déclare un compteur, et les
lignes 705 et 755 — les deux sites, celui du cache de décodage et celui du chemin
simple — appellent `CheckTicks()` (`sheepshaver_glue.cpp:971`) toutes les 50 000
instructions. Ce `CheckTicks()` fait les entrées, l'audio, le timer, la PRAM à
1 Hz et l'interruption VIA à 60 Hz.

**À faire** : la même couture, mais **vide de tout ce que notre tick fait déjà**.
Nous gardons l'IRQ Circle pour le 60 Hz (décision §7.3 du plan) ; ce qui doit y
aller est seulement ce qui a besoin du fil PowerPC et du droit de bloquer :
l'écriture PRAM, la synchronisation du dossier partagé, et le drain des entrées
du §3.2. Un `#define` dans notre config, pas un `#ifdef EMSCRIPTEN` recopié — et
comme cela touche `external/`, cela passe par `patches/macemu/`.

Attention au piège maison : les objets de `src/kernel/emu-sheepshaver/` ne
dépendent pas du Makefile, donc `make okapia-clean` après tout changement de flag.

### 4.2 Le Mac dit quand il ne fait rien, et nous jetons l'information

`idle_wait()` est appelé par le patch `SynchIdleTime` de la ROM : c'est l'invité
qui annonce qu'il n'a rien à faire. Chez nous, `main_circle.cpp:288` attend
100 µs et la préférence `idlewait` vaut `false` (`prefs_circle.cpp:68`, « untested
here ») ; côté PowerPC, `platform_bits_circle.cpp:64` est vide.

infinite-mac en tire trois choses : `timer_unix.cpp:360` y rend la main jusqu'à la
prochaine image attendue ; `rsrc_patches.cpp:57` expose `HasIdleTime()` pour savoir
si le patch s'est bien installé, et `main_unix.cpp:1306` retombe sur un `sleep`
quand ce n'est pas le cas ; et `worker.ts:357` s'en sert pour marquer la machine
*quiescente*, c'est-à-dire **démarrée**.

Ce troisième usage est celui qui nous manque le plus. `run-test.sh` et
`screenshot.sh` prennent aujourd'hui un nombre de secondes deviné — 3 s pour
System 7.1, 2 à 3 minutes pour 7.6.1 — et AGENTS.md porte déjà la cicatrice de ce
choix (« juger le boot à l'écran, pas à des métriques indirectes »). Un
« le Mac est au repos pour la première fois » est un signal donné par l'invité,
pas une approximation : c'est le moment de la capture, et le moment où le test
peut conclure.

**À faire**, dans cet ordre : (a) journaliser `HasIdleTime()` et le premier repos,
et s'en servir dans `screenshot.sh`/`run-test.sh` ; (b) seulement ensuite,
décider si `idle_wait()` doit vraiment rendre la main — sur un Pi le cœur libéré
ne sert à personne, le gain est thermique, et il faut le mesurer avant de le
prendre.

### 4.3 Un garde-fou sur les accès mémoire invités, en mode trace

`sysdeps.h` fait tester à `do_get_mem_long/word/byte` que l'adresse est dans le
tas avant de la déréférencer, et imprime l'adresse fautive sinon. Sous Circle,
une adresse invitée aberrante en `DIRECT_ADDRESSING` devient une lecture hôte
sauvage : au mieux un octet faux, au pire un abort de données dont le PC ne dit
rien de l'origine.

**À faire** : le même test, mais **uniquement sous `OKAPIA_TRACE=1`** et sans
`printf` dans le chemin — un compteur et une adresse retenue, imprimés par le
rapport périodique. Leur version imprime dans le chemin chaud, ce qui est
inacceptable ici.

## 5. Consolidation — un seul pilote vidéo pour deux Macintosh

`video_circle.cpp` (579 l.) et `sheepshaver/video_circle.cpp` (538 l.) font le
même travail, alimentent le même compositeur, et divergeront. C'est déjà 1 117
lignes pour un pilote.

infinite-mac n'écrit le sien qu'une fois : `video_js.cpp:23-86` est un talon
`#ifdef SHEEPSHAVER` qui fabrique pour SheepShaver ce que Basilisk fournit —
une classe `monitor_desc` abstraite, un `VideoMonitors`, un `IsDirectMode()`,
un `find_apple_resolution()` — après quoi `JS_monitor_desc` (ligne 91) et tout le
reste du fichier sont écrits une seule fois, en style Basilisk, et compilés deux
fois. La liste de modes est construite dynamiquement dans les deux cas.

**À faire** : le même talon, dans `src/circle/sheepshaver/` pour qu'il reste sur le
chemin d'inclusion du seul moteur PowerPC. Le travail n'est pas gratuit — nos deux
pilotes ont divergé — mais l'invariant qu'il installe l'est : **une correction
d'affichage cesse d'être à faire deux fois.**

Piège connu à ne pas rouvrir : `VideoMonitors.push_back()` une seule fois pour la
vie de la carte, et le framebuffer réclamé via `FwOutputClaim()`. Le Macintosh
repasse par là à chaque redémarrage.

## 6. À ne pas prendre

- **`SpookyHash` sur tout le framebuffer** (`video_js.cpp:208`). Ils hachent
  1,2 Mo à chaque image pour savoir si elle a changé, puis renvoient l'image
  entière. Notre compositeur compare 16×16 par `memcmp` avec sortie anticipée et
  ne recopie que les tuiles modifiées : 452 µs contre 74 637 µs sous fenêtre
  cocoa. **Nous sommes strictement devant. Ne pas régresser.** Le seul détail à
  retenir est le leur `hash ^= last_palette_hash` : un changement de palette doit
  invalider l'image — ce que `s_bFullRedraw` fait déjà chez nous.
- **Leur modèle de durabilité.** `disk-saver.ts` empile des chunks sales dans
  l'OPFS sans ordre d'écriture ni politique de flush. Il n'y a rien à en prendre
  pour une carte SD qu'on débranche.
- **`EMSCRIPTEN_HEAP_SIZE` codé en dur dans `sysdeps.h`** avec un commentaire
  disant qu'il doit rester d'accord avec un script shell. C'est exactement le
  genre de constante que `gen-strings.py` et `gen-keycodes.py` existent pour
  éviter ici.
- **Les `#ifdef EMSCRIPTEN` en cascade dans `external/`.** Notre
  `patches/macemu/0001` a généralisé le même besoin (« let a port place the guest
  itself ») au lieu d'ajouter une plateforme au `#if`. C'est la bonne forme, et
  c'est elle qui est remontable en amont.
- Le presse-papiers (`clip_js.cpp`) : il n'y a pas de presse-papiers hôte sur une
  carte nue.

## 7. Convergences déjà acquises — ne rien faire

Ces choix ont été pris des deux côtés indépendamment. Ils se confirment mutuellement
et n'appellent aucun travail :

- `uae_cpu_2021` forcé, quelle que soit l'architecture (`configure.ac`, option
  `--enable-uae_cpu_2021`) — notre invariant.
- `gZeroPage`/`gKernelData` statiques pour la mémoire basse et les données noyau
  PowerPC (`vm.hpp:210`) — notre `mac_layout.h` dit la même chose, en plus général.
- Pas de `SIGSEGV`, pas de VOSF, pas de JIT, pas de threads.
- Modèle audio *pull* : `AudioRefresh()` (`audio_js.cpp:209`) et notre
  `AudioPump()` lèvent tous deux `INTFLAG_AUDIO` quand la file a de la place, au
  lieu d'attendre une interruption périodique. Le seul détail qui leur manquait
  chez nous est **repris** : leur `close_grace_period` continue de réclamer dix
  blocs après que le mixeur a annoncé zéro source, parce qu'il l'annonce quand la
  dernière source a fini de *fournir* et non quand le dernier échantillon a été
  pris — sans quoi une alerte est coupée d'une fraction, ce qui s'entend comme un
  clic. Dix trames à 60 Hz, un sixième de seconde, et rien à payer une fois le
  mixeur réellement vide.
- La PRAM : ils comparent la PRAM une fois par seconde et se contentent de
  l'imprimer (`pram_helpers.h`) ; nous l'écrivons au moment où elle change. Notre
  contrainte de sécurité des données donne le bon comportement, la leur non.

## 8. À étudier, sans décider ici

- **Le gouverneur de vitesse** (`speed-governor.ts`) : une moyenne mobile
  exponentielle de l'erreur en millisecondes qui ramène l'invité à un nombre
  d'instructions par milliseconde visé. Utile si Okapia veut un jour une vitesse
  d'époque plutôt que « le plus vite possible » — beaucoup de jeux 68k en
  dépendent. Sans intérêt tant que nous cherchons de la vitesse.
- **L'overlay copie-sur-écriture** (`disk-saver.ts`) : image de base en lecture
  seule plus un bitmap de blocs modifiés. Cela donnerait un volume de secours
  **inscriptible et pourtant indestructible**, là où le préfixe `*` d'AGENTS.md
  donne un volume de secours en lecture seule. C'est séduisant et c'est
  exactement le genre de couche qui, selon nos propres règles, « doit sa
  politique de flush et son test dans le même changement ». À ne pas ouvrir avant
  que le second volume amorçable existe.
- **AppleTalk dans la PRAM par défaut** (`ether_helpers.h`) : cinq octets de PRAM
  qui font qu'un System trouve le réseau sans réglage. À reprendre quand la phase
  réseau arrivera, pas avant.

## 9. Ordre d'application proposé

| # | Élément | Où | État |
|---|---|---|---|
| 1 | Les deux convertisseurs 1 bit ignorent la palette | `patches/macemu/0003` | **fait**, `tests/host/check_blit.cpp` |
| 2 | Drain des entrées dans le fil d'émulation | `input_circle.cpp`, `cpu_ticks_circle.cpp` | **fait** |
| 3 | Couture périodique PowerPC | `patches/macemu/0004`, `sheepshaver/cpu_ticks_circle.cpp` | **fait** |
| 4 | `HasIdleTime()` + repos journalisé | `patches/macemu/0005`, `main_circle.cpp`, scripts | **fait** |
| 5 | Encodage des noms ExtFS | `mac_encoding_circle.cpp`, `gen-macroman.py` | **fait**, `tests/host/check_encoding.cpp` |
| 6 | Média amovible via `disk_generic` | `patches/macemu/0006` | **fait** (amont seul : Okapia n'a pas encore de gestionnaire) |
| 7 | Garde-fou mémoire sous trace | `patches/macemu/0007`, `main_circle.cpp` | **fait**, `OKAPIA_TRACE=1` |
| 8 | Pilote vidéo mutualisé | `video_shared_circle.cpp` + deux adaptateurs | **fait** |

Les sept premiers étaient indépendants les uns des autres. Le huitième est une
refonte, et il attendait que les autres soient posés — sans quoi il aurait fallu
les faire deux fois.

**Le huitième n'a pas pris la forme qu'infinite-mac lui donne**, et c'est
délibéré. Leur solution est un talon `#ifdef SHEEPSHAVER` qui fabrique un faux
`monitor_desc` pour que le fichier de Basilisk compile des deux côtés
(`video_js.cpp:23-86`). Cela marche chez eux parce que leur pilote est simple ;
le nôtre ne l'est pas — régions sales, changement de mode par le pilote du Mac,
table `VModes` que `video.cpp` possède — et le talon aurait dû faire semblant sur
tout cela. Okapia sépare plutôt **la règle de la traduction** : les 418 lignes de
`video_shared_circle.cpp` disent une seule fois quels modes tiennent, quel
convertisseur va avec quelle profondeur, comment la palette devient une table,
à quelle cadence composer et quoi en dire ; chaque `video_circle.cpp` ne garde
que le contrat de son moteur. 579 + 538 lignes dont la moitié était en double
deviennent 266 + 370 qui ne le sont plus.

Mesuré après la bascule, sur les deux moteurs : composite 1 123 µs et 7 % du
temps mural côté 68k, 1 210 µs et 7,2 % côté PowerPC, écran à 59-60 Hz des deux
côtés — les mêmes chiffres qu'avant — et les deux Finder capturés à l'écran.

Ce que la mise en œuvre a mesuré :

- **La couture PowerPC tourne à 112 Hz** sous QEMU avec `PPC_CHECK_TICKS=50000`
  (`okapia-ppc: periodic seam`), et la PRAM du Macintosh PowerPC est désormais
  écrite en cours de session — cinq fois pendant un démarrage de System 7.6, là
  où ce moteur n'en écrivait aucune. C'est le manque le plus concret que l'étude
  avait relevé, et il est comblé.
- **System 7.1 signale son repos à 11,5 s** ; System 7.6 ne le signale jamais, et
  pour une raison qui n'était pas dans l'analyse : la carte par défaut démarre le
  moteur **PowerPC**, et `patch_idle_time()` n'existe pas dans l'arbre
  SheepShaver. Le signal est donc une propriété du moteur 68k, pas du System.

Deux points où l'exécution s'est écartée de l'analyse, et pourquoi :

- **La correction du convertisseur 1 bit vaut pour les deux profondeurs.**
  infinite-mac n'a corrigé que la version 32 bits, la seule dont il avait besoin ;
  `Blit_Expand_1_To_16` porte le même défaut et est corrigé ici aussi.
- **`run-test.sh` ne raccourcit pas sa durée quand le Mac dit qu'il a démarré.**
  Tuer un Macintosh au repos est le cas facile, et ce test existe pour le cas
  dur. Le signal sert donc à refuser un verdict vert obtenu par un démarrage qui
  n'a jamais abouti, pas à finir plus tôt. `screenshot.sh`, lui, capture dès le
  signal : il n'a rien à éprouver.

Et deux défauts trouvés en chemin, corrigés parce que les changements ci-dessus
les rendaient actifs :

- **Les deux Macintosh écrivaient la même PRAM.** Celle de SheepShaver fait
  8192 octets, celle de Basilisk 256, et les champs ne sont pas aux mêmes
  positions. Tant que le moteur PowerPC n'écrivait jamais rien, cela ne se
  voyait pas ; la couture périodique le fait écrire. Un fichier par moteur, et
  une lecture d'un octet de trop pour qu'un fichier *plus long* soit refusé au
  lieu d'être pris pour une lecture courte réussie.
- **`run-test.sh` rendait un verdict vert sur un volume choisi au hasard** quand
  la carte démarre le moteur PowerPC, parce que `kernel_ppc.cpp` ne journalisait
  aucun `Boot volume:`. C'est exactement le piège qu'AGENTS.md décrit. Le noyau
  PowerPC nomme désormais son premier disque configuré, et le test répond
  INCONCLUSIVE plutôt qu'OK quand personne ne l'a nommé.

## 10. FPU et performance

### 10.1 Le FPU — le seul point où nous étions nettement derrière

Le routage vers MPFR sur ARM est **d'amont**, pas de mihaip :
`external/macemu/BasiliskII/src/Unix/configure.ac:1881` envoie déjà `arm` et
`aarch64` sur `fpu_mpfr.cpp` avec `-lmpfr -lgmp`. mihaip n'a fait qu'ajouter
`-o "x$WANT_EMSCRIPTEN" = "xyes"` à la même condition. Autrement dit : **tous
ceux qui font tourner ce code sur AArch64, y compris le portage WebAssembly,
utilisent MPFR. Nous sommes les seuls à ne pas le faire.**

Et le prix qu'ils ont accepté de payer se mesure : `_em_build_mpfr.sh` compile
GMP 6.2.1 puis MPFR 4.1.1 vers WebAssembly avant de compiler l'émulateur.
Personne ne fait cela pour le plaisir.

Ce que `FPU_UAE` nous donne, précisément : `uae_cpu_2021/fpu/types.h:67` fait de
`fpu_register` un `uae_f64`, c'est-à-dire un **`double` de 8 octets**. Le 68881
a des registres étendus de 80 bits. Nous donnons donc à l'invité 53 bits de
mantisse là où le Macintosh en promet 64, et un exposant de ±308 là où il promet
±4932. Un `FMOVE.X` qui écrit puis relit perd des bits ; les seuils de
débordement sont faux.

**Ce que l'analyse avait conclu, et qui était faux.** `FPU_IEEE` ne donne
l'étendu 80 bits que sur x86, où `long double` est le format du x87
(`types.h:139-145`) ; la variante quad — `long double` en binary128, ce qu'est
justement le nôtre sur AArch64 — est dans le fichier et **désactivée**, motif
écrit à `types.h:147-150` : « l'implémentation de l'émulateur n'est pas
correcte ». J'en avais déduit « le choix est binaire : `double` ou MPFR ». Il ne
l'était pas : le motif décrivait un bug réparable, pas une impossibilité. Voir la
suite.

`planification.md` §15 R8 enregistrait le risque comme accepté (« peu
d'applications d'époque en dépendent finement ; MPFR reste possible plus tard »).

**C'est réglé, et pas par MPFR.** L'analyse ci-dessus était juste sur le
diagnostic et fausse sur les options : elle concluait « le choix est binaire —
`double` ou MPFR ». Il y en avait une troisième, dans le fichier, désactivée
depuis vingt ans. Sur AArch64 `long double` **est** l'IEEE binary128 : exposant
de 15 bits au biais 16383, celui du Macintosh lui-même, et 112 bits de fraction
pour ses 63. Le registre 68881 y tient exactement, sans bibliothèque et sans
allocation. `types.h:147-150` refusait ce format sur deux objections ; les deux
ont été vérifiées, l'une a expiré et l'autre était un vrai bug — `make_extended()`
n'était pas l'inverse de `extract_extended()`, **1.0 entrait et 1.5 ressortait**.
`patches/macemu/0008` fait les deux : il active le format et il corrige le code.

Ce que MPFR aurait coûté, pour mémoire : `libgmp.a` 829 Ko et `libmpfr.a` 787 Ko
en statique, contre environ 1 Mo de marge sous les 4 Mo de `KERNEL_MAX_SIZE`, un
portage de deux bibliothèques autotools vers le bare-metal, et surtout **un
`malloc` et un `free` par instruction flottante** (`fpu_mpfr.cpp:1517`, dans
`fpuop_general`) — ce que la règle de ressources de ce projet interdit
explicitement. binary128 coûte 62 Ko et rien d'autre.

Ce que cela ne change **pas** : le moteur PowerPC. `ppc-registers.hpp:161` fait
d'un registre flottant PowerPC une union avec un `double` hôte — ce que
l'architecture spécifie. **Le flottant de SheepShaver était déjà exact ;** seul le
côté 68k était tronqué.

Une mesure vaut d'être retenue au passage : un démarrage de System 7.1 jusqu'au
Finder exécute **quatre** instructions flottantes (`trace_fpu_circle.cpp`,
`--wrap` sur `fpuop_arithmetic`). C'est pour cela qu'un démarrage réussi ne
prouve rien sur ce chemin, et pour cela qu'un bug aussi gros que 1.0 → 1.5 a pu
rester en place si longtemps.

### 10.2 Le reste, réglage par réglage

| Réglage | infinite-mac | Okapia | Remarque |
|---|---|---|---|
| JIT | `--disable-jit-compiler` | non | chez nous le tas est `PXN=1` |
| VOSF | `--disable-vosf` | non | pas de `SIGSEGV` des deux côtés |
| `OPTIMIZED_FLAGS` | `#undef` (`em_config.h`) | sans objet | c'est de l'assembleur x86 |
| `uae_cpu_2021` | forcé (`--enable-uae_cpu_2021`) | invariant | convergent |
| **Niveau d'optimisation** | `-O3` | `-O3` (mesuré, voir plus bas) | `src/kernel/Makefile`, avant `Rules.mk` |
| `PPC_DECODE_CACHE` | défaut (1) | défaut (1) | `ppc-config.hpp:61` |
| `PPC_ENABLE_FPU_EXCEPTIONS` | défaut (0) | défaut (0) | « plus lent et pas encore correct » |
| Coût de la couture périodique | compteur par instruction PPC | `--emulated_ticks` d'amont | ils paient plus |
| Détection d'écran changé | hachage de l'image entière | `memcmp` par tuile | nous sommes devant |
| `video_set_dirty_area()` | **vide** (`video_js.cpp:352`) | implémenté | **nous sommes devant** |

Deux points méritent d'être retenus.

**`-O3` était la seule expérience gratuite de cette étude, et elle a été faite.**
`OPTIMIZE` est un `?=` dans `Rules.mk:238`, donc une ligne avant l'inclusion
suffit. Résultat sous QEMU, System 7.1 jusqu'au Finder, deux exécutions
chacun :

| | temps jusqu'au repos | k opcodes/s à 5, 10, 15 s | image |
|---|---|---|---|
| `-O2` | 11 946 et 12 017 ms | 2988, 2745, 2970 | 2 817 Ko |
| `-O3` | 11 933 et 12 024 ms | 2739, 2667, 2905 | 2 992 Ko |

Le temps de démarrage ne bouge pas, et le débit d'opcodes **baisse de 2 à 8 %**,
deux fois de suite à chaque relevé, pour 175 Ko de plus. Ce verdict-là est celui
de QEMU et ne tranche rien : QEMU traduit l'AArch64 vers l'hôte, donc il
récompense le nombre d'instructions et il est aveugle à la prédiction de
branchement et à la localité de cache que `-O3` vise.

**Ce qui tranche, c'est que `-O3` casse le menu de démarrage.** Il vectorise la
recopie de `GfxBlit()` (`okapia_gfx.cpp:113`) en accès NEON larges, à
l'alignement que lui donne le rectangle endommagé, et la destination est le
framebuffer de sortie — que Circle place en mémoire **Device**
(`translationtable64.cpp:145` : tout ce qui est au-dessus de la part ARM de la
RAM, et le framebuffer du GPU est exactement là). La mémoire Device exige que
chaque accès soit naturellement aligné : le premier magasin vectoriel non aligné
est une faute d'alignement. Mesuré : le menu s'affiche, la carte meurt une
seconde plus tard, `EC 0x25`, `DFSC 0x21`, sur une écriture à 0x3C3xxxxx.

`-O2` est donc rétabli, et le Makefile porte les deux raisons. `-O3` ne
redeviendra envisageable que le jour où tout chemin qui écrit le framebuffer
sera prouvé aligné.

**Sur les régions sales, c'est nous qui avons l'avance.** `gfxaccel.cpp:63`
appelle `video_set_dirty_area()` : c'est QuickDraw qui dit ce qu'il vient de
changer, une information qu'aucune comparaison ne peut retrouver. infinite-mac
la jette (`video_js.cpp:352` ne fait qu'un `D(bug)`) parce que le navigateur
recopie l'image entière de toute façon ;
`sheepshaver/video_circle.cpp:438` la donne au compositeur. L'unification des
deux pilotes (§5) l'a préservée.

**L'étendre au moteur 68k n'a pas de sens, et la mesure le dit.** Basilisk n'a
aucun crochet d'accélération — pas de NQD, rien qui annonce quoi que ce soit — donc
la seule source possible serait de marquer les tuiles à l'écriture 68k, soit une
comparaison sur chaque écriture invitée dans la boucle la plus chaude. Or le coût
n'était pas là : un écran au repos affichait **0/256 boîtes et 1 225 µs**, c'est-à-dire
que tout partait dans la comparaison et rien dans le dessin. Deux changements dans le
compositeur partagé, qui ne coûtent rien à l'émulation, l'ont ramené à **198 µs et
1,1 %** au repos : les tuiles sales voisines sont dessinées en une bande, et le
balayage bon marché — là où l'écran bougeait, plus une tuile de marge, plus un huitième
du reste — ne sert qu'à décider s'il y a quelque chose ; dès qu'il trouve, tout le reste
est comparé avant qu'un pixel ne sorte. **Étaler comme l'amont et dessiner ce que
l'huitième a trouvé a été essayé et rejeté** : c'était plus rapide au chiffre et faux à
l'œil, un menu descendant en mosaïque sur huit trames. L'invité récupère la différence :
2 880 → 3 031 k opcodes/s.

## 11. Sources

- `reference/infinite-mac/` — https://github.com/mihaip/infinite-mac (MIT pour le
  site, GPL pour les émulateurs).
- `reference/infinite-mac/macemu/` — https://github.com/mihaip/macemu, branche
  `infinite-mac-kanjitalk755`, base commune `d7c0303`.
- Toutes les lignes citées de `reference/` sont à ce commit ; celles de `src/` et
  `external/` sont à l'état du dépôt au moment de l'étude.
