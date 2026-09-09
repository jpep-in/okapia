# Le pointeur saccadait — enquête et corrections

Session du 8 septembre 2026. Point de départ : sous Mac OS le pointeur accrochait, alors que
dans le menu de démarrage il était parfaitement fluide. Tout ce qui suit a été mesuré sous
QEMU `raspi3b` avec fenêtre `cocoa`, sur System 7.1 (`qemu/sd-contents/boot71.img`), sauf
mention contraire.

## Ce qui n'allait pas

Quatre défauts indépendants, trouvés dans cet ordre — qui n'est pas celui de leur importance.

### 1. Le compositeur perdait de vue le pointeur (le principal)

`CompositorRun()` finissait par `memcpy(Watched, Moving)`. `Moving` ne contient que les tuiles
**sales de la trame courante**, donc une seule trame immobile effaçait la mémoire du
compositeur. À basse vitesse le pointeur n'avance que d'un pixel toutes les deux ou trois
trames : entre ses propres pas le jeu surveillé se vidait, et le pas suivant n'était plus dans
le jeu bon marché — il devait attendre son tour dans la rotation par huitièmes. **Jusqu'à
133 ms, au hasard.**

Ressenti : « le pointeur accroche un instant puis reprend sa route, surtout à basse vitesse ».
**Invisible dans les chiffres**, parce que ce sont des trames où le compositeur a correctement
conclu que rien n'avait changé.

Correction : un compte à rebours de 12 trames par tuile (`WatchFor[16][16]`, 256 octets et une
passe de décréments) au lieu d'une mémoire d'une seule trame.

### 2. Le VBL battait en 20/10/20

`PeriodicHandler()` émet les ticks Mac depuis un accumulateur alimenté à `HZ`, qui vaut **100**
dans Circle (`timer.h:30`). Une tick Mac fait 16625 µs, qui ne divise pas 10000 : les
intervalles sortent en 20, 10, 20, 20, 10 ms. La moyenne est exacte, **une trame sur trois est
deux fois plus courte que ses voisines**, et le Macintosh redessine son pointeur sur ce rythme.

Mesuré, et invariant sur seize fenêtres de cinq secondes, au repos comme en balayage rapide :

```
avant : min 8830 us, max 21208 us,  8:3 9:54 10:43 11:2 │ 18:4 19:105 20:83 21:10
        103 intervalles courts : 202 longs, dans chaque fenêtre
apres : min 15454 us, max 17816 us,           15:11 16:279 17:14
```

Correction : `CUserTimer` (`usertimer.h`) programme le comparateur du timer système avec un
délai en microsecondes, réarmé depuis son propre handler sur une échéance absolue. Le
`RegisterPeriodicHandler` reste en repli, choisi à la compilation par `RASPPI <= 4` — au-delà
c'est le Pi 5, dont on ne suppose rien. Le noyau journalise lequel est en service.

### 3. Une tick en retard était remboursée

Circle réarme son propre comparateur d'une seule période quelle que soit la latence
(`timer.cpp:577`), donc une interruption tardive rappelle le handler aussitôt ; notre `while`
ajoutait son propre rattrapage par-dessus. Résultat mesuré une fois : **seize ticks à 0 ms
d'intervalle suivies d'un trou de 140 ms.**

Correction : une tick au plus par visite, et resynchronisation quand on a pris du retard.
C'est la règle d'upstream — `main_unix.cpp:1348` fait exactement cela dans son thread 60 Hz.

### 4. La vidange était comptée en opcodes, pas en temps

`cpu_do_check_ticks()` se déclenche au débordement d'`emulated_ticks`, soit tous les 65536
opcodes. C'est une cadence en temps **seulement si la vitesse du moteur est fixe**, et elle ne
l'est pas : mesuré entre **209 et 1076 passages** par fenêtre de cinq secondes, soit une
vidange toutes les 5 à 24 ms. Le commentaire du fichier annonçait quatre millisecondes.

Conséquence : la souris rapportant à ~100 Hz, plusieurs rapports fusionnaient en un seul paquet
ADB. Amplitudes mesurées jusqu'à **275 counts en un paquet** là où une souris ADB de 200 cpi
n'en envoyait jamais plus d'une dizaine — la courbe d'accélération du Mac extrapolait une
vitesse impossible et le pointeur se téléportait.

Correction : le quantum d'opcodes est mesuré contre l'horloge toutes les 64 visites et corrigé,
pour tenir 1 kHz quelle que soit la vitesse d'interprétation. `infinite-mac` fait la même chose
pour la même raison — c'est l'autre port sans thread à dépenser (`main_unix.cpp:342`,
« Recalibrate 1000 Hz quantum every 10 ticks »).

Mesuré après : **4997 vidanges en 5 s, soit 999,4/s**, quantum stabilisé vers 2800.

**Pourquoi 1 kHz et pas 100 Hz** : la vidange doit être nettement plus rapide que la source
d'entrée la plus rapide, sinon les événements fusionnent. La preuve est dans les données — sur
quatre fenêtres avec mouvement, trois donnent un paquet par rapport USB (228→228, 530→530,
179→179) et la quatrième, la seule où la vidange était tombée à 668/s, donne 390→374. À 100 Hz
la fusion serait la règle. Débit maximal vers le Mac : **106 paquets/s**, soit la cadence d'une
vraie ADB.

## Ce qui a été construit puis retiré

Une souris **absolue** pour le moteur 68k, sur le modèle de tous les autres ports
(`video_macosx.mm:464`, `video_sdl.cpp:723`, `input_js.cpp:13` appellent tous
`ADBSetRelMouseMode(false)`), avec notre propre accélération, une rampe continue, un
coefficient `mousespeed` et la lecture du tableau de bord Souris.

**Retiré sur décision, et c'était le bon choix.** Une fois la vidange corrigée, les paquets ADB
retrouvent la taille d'un rapport de souris, la courbe du Macintosh les traite comme prévu, et
le tableau de bord Souris fonctionne nativement — sans notre approximation de sa courbe.
Le mode relatif est donc à la fois plus simple et plus fidèle.

Ce qui a été perdu au passage, et qui était réel : le pointeur ne rattrape plus la position de
l'hôte (le comportement de Basilisk sur macOS), et le menu de démarrage et le Mac gardent deux
pointeurs distincts.

## L'ablation : qu'est-ce qui comptait vraiment

Question posée en fin de session, et légitime. Cinq noyaux construits, les corrections retirées
une à une, l'arbre restauré ensuite et vérifié reconstruire **bit pour bit identique**.

| noyau | compositeur | VBL | vidange | souris |
|---|---|---|---|---|
| K0 | mémoire d'une trame | grille 10 ms | 65536 opcodes | du Mac |
| K1 | **12 trames** | grille 10 ms | 65536 opcodes | du Mac |
| K1a | 12 trames | **échéance µs** | 65536 opcodes | du Mac |
| K1b | 12 trames | échéance µs | **1 kHz** | du Mac |
| K2 | 12 trames | échéance µs | 1 kHz | **absolue, notre courbe** |

**Verdict à l'usage : K1 corrige 80 % du problème à lui seul.** Le compositeur était le vrai
coupable ; le battement du VBL et la cadence de la vidange sont des défauts réels mais
secondaires. K2 a été écarté — il changeait le comportement sans rien apporter.

Configuration retenue : **K1b**.

À retenir pour la prochaine fois : le défaut le plus coûteux à l'usage était celui qui
n'apparaissait dans **aucune mesure**, et les trois autres ont été trouvés en mesurant. Les
deux approches étaient nécessaires ; aucune ne suffisait.

## Faits établis, à ne pas re-chercher

- **`HZ` vaut 100 dans Circle** (`timer.h:30`), et aucun réglage du projet ne le change. Toute
  cadence dérivée d'un accumulateur sur cette grille bat en 20/10/20.
- **`CrsrThresh` (0x8EC) ne bouge pas sous System 7.1.** Vérifié sur huit changements du
  curseur du tableau de bord : il reste à 6, sa valeur de démarrage (`StartInit.a:2944`).
- **Le tableau de bord Souris écrit `SPVolCtl` (0x208), bits 5:3.** Balayé dans les deux sens :
  6 5 4 3 2 1 0 puis 0 1 2 3 4 5 6. Sept positions — **tablette = 0, « Lent » = 1, « Rapide »
  = 6**, et 7 n'est jamais atteint. La PRAM (`XPRAM[0x20]` chez Basilisk, la PRAM classique
  commençant à `XPRAM[0x10]`, `main.cpp:115`) reste à zéro : le cdev n'y écrit pas.
- **La souris négocie le protocole ADB étendu** (`Listen reg3` sur le périphérique 3, handler
  ID 4), donc le champ de delta fait 10 bits et non 7. Le plus gros delta jamais mesuré est
  376 : **la troncature n'a jamais mordu.**
- **`CursorDeviceDispatch` : le sélecteur 0 (`Move`) accélère, le sélecteur 1 (`MoveTo`) non**
  (`CrsrDev.a:137`). Basilisk appelle `MoveTo` (`adb.cpp:405`), donc **le tableau de bord
  Souris est inopérant sous SheepShaver par construction**, et cela précède cette session.
- **Le seam PowerPC tourne à ~135 Hz** avec `PPC_CHECK_TICKS=50000`, et la moyenne cumulée de
  87 Hz montre qu'il a été plus lent ailleurs dans la session : même défaut de nature que les
  65536 opcodes du 68k.
- **Un mouvement lent du pointeur avance par pas de `nScale` pixels d'écran.** C'est le
  grossissement, pas un défaut ; seul un mode invité égal à la sortie l'efface. Le mode par
  défaut reste figé à 640×480 : sur une sortie qui n'en est pas un multiple entier, le
  Macintosh occupe donc une partie de l'écran dans un cadre noir.

## Ce qui reste ouvert

- **Le seam PowerPC.** Le corriger demande de transformer `PPC_CHECK_TICKS` en variable dans
  `patches/macemu/0004`, la calibration étant reprise telle quelle du 68k. **Pas urgent** : la
  souris de SheepShaver étant absolue, une fusion de rapports y est sans conséquence — seule la
  dernière position compte. Ce qu'on y perd est ~7 ms de latence et toute garantie sur un autre
  matériel.
- **Le tableau de bord sous SheepShaver.** Le rendre opérant demande un troisième chemin :
  appeler `CursorDeviceDispatch` avec le sélecteur 0 et nos deltas, au lieu du sélecteur 1 avec
  une position. Faisable sans toucher `external/`, mais c'est du code neuf qui exécute du 68k
  depuis la vidange, avec une convention d'appel tirée d'un listing de 1994.
- **Tout re-mesurer sur le Pi 4.** Le battement 20/10/20 est de l'arithmétique et tiendra tel
  quel. Le reste ne s'extrapole pas : la vidange y sera bien plus rapide (l'interpréteur l'est),
  et le coût du compositeur y est d'une autre nature — le framebuffer de sortie est en mémoire
  Device, non cachée, là où QEMU facturait son suivi de pages sales. **`CUserTimer` n'a jamais
  tourné sur du vrai matériel** : la ligne `VBL spacing` du log est ce qui le dira.

## Où ces corrections ont atterri

Le code de cette session a été commité avec d'autres travaux en cours. Les messages de commit
ont été complétés après coup pour dire ce qu'ils emportaient ; cette table reste l'index le
plus rapide.

| correction | commit |
|---|---|
| compositeur, compte à rebours des tuiles | `31d44f2` *compositor: never draw the screen from a partial scan* |
| cadence de la vidange, quantum recalibré | `ee9b0ef` *input: hand the events to adb.cpp from the core that reads them* |
| cadence du VBL, `CUserTimer` et non-remboursement | `3dff321` *firmware: the engine seam the previous commits left behind* |
| garde-fou `patches-check` | `768eea4` *fpu: give the 68881 a register…, and fix two blitters* |

## Fichiers touchés

| fichier | ce qui change |
|---|---|
| `src/circle/compositor_circle.{cpp,h}` | `WatchFor[16][16]`, compte à rebours de 12 trames |
| `src/circle/tick_circle.cpp` | `CUserTimer` avec repli, plus de remboursement, histogramme `VBL spacing` |
| `src/circle/cpu_ticks_circle.cpp` | quantum recalibré à 1 kHz, comptabilité des opcodes |
| `src/circle/input_circle.cpp` | compteurs de vidange ; la branche souris est revenue à son état d'origine |
| `src/circle/prefs_circle.cpp` | inchangé au net — `mousespeed` ajouté puis retiré avec l'absolu |

Aucun fichier vidéo n'est touché en dehors du compositeur : `video_circle.cpp`,
`video_shared_circle.{cpp,h}` et `sheepshaver/video_circle.cpp` sont dans leur état d'avant la
session.
