# La souris du Macintosh PowerPC, et ce que le ROM en dit

Session du 9 septembre 2026, branche `ppc_mouse_improv`. Deux demandes au départ : donner au
seam PowerPC une cadence tenue par l'horloge, et rendre le tableau de bord Souris opérant sous
SheepShaver. Mesuré sous QEMU `raspi3b`, sur Mac OS 8.6 (`qemu/sd-contents/volume1.image`,
moteur PowerPC) et System 7.1 (`boot71.img`, moteur 68k).

## Le seam PowerPC

`PPC_CHECK_TICKS` était un compte d'instructions figé à 50000, et un compte d'instructions n'est
une cadence que sur une machine dont la vitesse ne change pas. Mesuré : **87 Hz en moyenne sur
un run dont la dernière fenêtre était à 116** — la cadence suivait ce que l'invité faisait.

`patches/macemu/0004` compare désormais à une variable, `ppc_check_ticks_quantum`, dont
`PPC_CHECK_TICKS` est la valeur initiale ; un port qui ne la touche pas garde exactement
l'ancien comportement. L'arithmétique de calibration est celle du 68k, reprise sans la modifier.

Mesuré après : **~950 Hz**, quantum stabilisé vers 7000, et retombant sur son plancher quand
l'invité rame — ce qui est le comportement voulu et non un défaut.

**Où le symbole doit vivre.** `ppc_check_ticks_quantum` est déclaré dans `ppc-cpu.hpp` (patché)
et défini dans `sheepshaver/cpu_ticks_circle.cpp`. Les deux sont dans la moitié PowerPC, donc la
définition et la référence sont renommées ensemble par le préfixe `ppc__` et ça lie. **Le définir
dans un fichier partagé aurait cassé** : la référence serait devenue `ppc__ppc_check_ticks_quantum`
pendant que la définition gardait son nom nu.

## Pourquoi le tableau de bord ne faisait rien

Par construction, et c'est antérieur à cette session. Le moteur PowerPC donne au Mac une
**position**, via `CursorDeviceDispatch` sélecteur 1, `MoveTo` (`adb.cpp:405`). Or seul le
sélecteur 0, `Move`, fait passer des deltas par les tables d'accélération (`CrsrDev.a:137`) :

```
@jmpTable  dc.w  CrsrDevMoveTrap    ; sélecteur 0  — « dh and dv will be run
           dc.w  CrsrDevMoveToTrap  ; sélecteur 1     through the acceleration algorithm »
```

`MoveTo` pose le curseur où on le lui dit. Le curseur du tableau de bord pilote
`CrsrDevSetAccel` (sélecteur 8), qui remplit des tables que `MoveTo` ne consulte jamais.

## La courbe du ROM, décodée

Appeler le sélecteur 0 aurait voulu dire exécuter du 68k **à chaque rapport de souris**. La
solution retenue fait la même arithmétique de notre côté, sans code invité — et la courbe n'est
pas inventée, c'est celle du ROM.

`MiscROMRsrcs.r`, ressource `'accl'` 1, `classMouse`, table marquée « New, better-feeling »,
en Fixed 16.16 :

| vitesse entrée (po/s) | vitesse sortie | gain |
|---|---|---|
| 0,44 | 0,375 | **0,85** |
| 4,31 | 16,5 | 3,83 |
| 12,0 | 95,0 | **7,92** |
| 22,9 | 139,0 | 6,06 |
| 29,2 | 148,5 | 5,08 |
| 34,5 → 40,0 | 150,0 | plafond |

Trois propriétés qu'une courbe maison n'aurait pas eues, et qui expliquent pourquoi la mienne
ne « sentait » pas juste :

1. **le gain est inférieur à 1 aux très basses vitesses** — le Mac *ralentit* le pointeur pour
   viser, il ne se contente pas de ne pas l'accélérer ;
2. **le maximum est au milieu**, ~8× vers 12 po/s, pas en haut ;
3. **la sortie sature à 150 po/s** — un plafond de *vitesse*, pas de gain.

### Les unités, qui sont la clé

`CrsrDev.a:1257` les fixe sans ambiguïté :

```asm
move.l  resolution(a2),-(sp)      ; dpi du périphérique
move.l  #(frameRate<<16),-(sp)
_FixDiv
move.l  (sp)+,d6                  ; = facteur d'échelle vitesse périphérique
```

Les nombres de la table sont des **pouces par seconde**, convertis en counts par
`dpi / frameRate` et en pixels par `screenRes / frameRate`.

| constante | valeur | source |
|---|---|---|
| `frameRate` | 67 trames/s | `CrsrDevEqu.a:150` |
| `screenRes` | 72 dpi (champ, initialisé) | `CrsrDev.a:2232` |
| résolution supposée du périphérique | **200 dpi** | `CrsrDev.a:2045` |

Le Mac étiquette même la souris `'@200'` et traite de « stupid 4th party device » tout
périphérique qui refuse le protocole.

### Le curseur du tableau de bord est une interpolation

```
resource 'accl' (1) { classMouse, {
    0 fdiv 1, { 1:1, 1:1 },      /* accélération 0.0 → table identité */
    1 fdiv 1, { la courbe }       /* accélération 1.0 → la courbe */
}};
```

`CrsrDevSetAccel` interpole entre les deux tables encadrantes (`CrsrDev.a:1102`). Le réglage est
donc un Fixed entre 0 et 1, et **la position tablette est l'identité** — ce n'est pas une
convention, c'est le résultat de l'interpolation à zéro.

## Où vit le réglage : mesuré, pas déduit

Le ROM offre deux domiciles plausibles et System 7 n'en utilise qu'un. Sur huit changements du
curseur, puis sur un balayage dans les deux sens :

| source | comportement |
|---|---|
| `CrsrThresh` (0x8EC) | **immobile à 6**, sa valeur de démarrage (`StartInit.a:2944`) |
| PRAM (`XPRAM[0x20]`, PRAM classique à `XPRAM[0x10]`, `main.cpp:115`) | **immobile à 0** |
| **`SPVolCtl` (0x208), bits 5:3** | suit chaque position |

Balayage descendant `6 5 4 3 2 1 0` puis montant `0 1 2 3 4 5 6`. **Sept positions : tablette
à 0, « Lent » à 1, « Rapide » à 6**, et 7 n'est jamais atteint. Vérifié **sur System 7.1 et sur
Mac OS 8.6**, même adresse, même encodage.

## Dire la vérité au Macintosh, côté 68k

Le moteur 68k n'a pas ce problème : il donne des deltas et le Mac accélère lui-même. Mais il
échelonne sa courbe sur une résolution qu'il **croit** — 200 — sans aucun moyen d'apprendre
autre chose. Une souris moderne est donc accélérée comme si elle allait cinq à huit fois moins
vite qu'en réalité.

`CrsrDevSetUnitsPerInch` (sélecteur 10) existe pour ça : *« May be called if the software knows
more about the resolution of the device than can be found from the ADB bus »*, et il recalcule
les tables à partir du nouveau chiffre (`CrsrDev.a:488`). Deux appels, une fois, après que le
Macintosh a annoncé qu'il avait fini de démarrer. **Le sélecteur 11 parcourt la liste globale des
périphériques** (`CrsrDev.a:516`) plutôt que de deviner un pointeur — un mauvais pointeur ferait
écrire une résolution par le ROM dans ce qu'il désignerait.

Vérifié sur System 7.1 : enregistrement trouvé à `0x25680`, appel accepté, le Macintosh continue.

## Le réglage est dans le menu de démarrage

Six valeurs, 100 à 1000, l'unité portée par la valeur pour que l'étiquette reste courte dans
toutes les langues. Une carte portant une valeur hors menu revient sur la plus proche offerte.

Le raisonnement « c'est une description du matériel, sa place est dans le fichier de
préférences » était le mauvais : corriger ce réglage, c'est corriger le confort du pointeur, et
personne ne doit sortir la carte et trouver un autre ordinateur pour ça. « dpi » est par ailleurs
un mot que connaît qui installe un émulateur bare-metal.

**Le défaut est 200**, ce que le Macintosh suppose de lui-même : une carte qui ne dit rien garde
exactement le comportement d'avant. Un réglage neuf qui change le ressenti de ceux qui ne l'ont
pas demandé est un bug, si juste soit son arithmétique — leçon apprise en le mettant à 1000 et en
rendant la souris cinq fois plus lente sous 7.1.

## Trois pièges rencontrés

### Une clé de préférence non déclarée est lue puis jetée, en silence

`mousedpi` avait son défaut mais pas son entrée dans la table de `prefs_circle.cpp`. La ligne sur
la carte se lisait, ne faisait rien, et ne le disait pas. **Une mesure entière a été faite
contre** : le pointeur a été jugé à un dixième de la vitesse qu'on croyait, décrit comme « une
souris à boule extrêmement encrassée », et la même courbe est devenue « turbo » dès que le
chiffre a été réellement lu. Rien de la courbe n'avait changé.

### Le Toolbox ne s'appelle pas depuis la vidange

Le stub demande un bloc au gestionnaire de mémoire et le rend, donc réentre dans le Toolbox.
Depuis la vidange, c'est entre deux instructions quelconques — éventuellement pendant que le Mac
est lui-même dans le gestionnaire de mémoire. Un tas corrompu là ressort **plus tard et
ailleurs** : mesuré une fois comme une faute de données dans `DiskInterrupt()`, une seconde et
demie après, sans rien pour relier les deux.

`idle_wait()` est le Macintosh annonçant qu'il n'a plus de travail, depuis sa propre boucle
d'événements : un point défini, le Système entièrement levé, rien de nôtre à moitié fait. Quatre
essais de vingt secondes, propres.

### Une réservation matérielle depuis un chemin de démarrage doit être partagée

`CUserTimer` connecte `ARM_IRQ_TIMER1`, et `interrupt.cpp:145` fait
`assert (m_apIRQHandler[nIRQ] == 0)`. Une assertion Circle halte la carte — ce qui, sous QEMU,
ressemble à l'émulateur qui quitte tout seul, et c'est exactement comme ça que le symptôme a été
rapporté : « ça plante, peut-être juste qemu qui quitte ».

La garde existait, mais dans `tick_circle.cpp`, qui est dans `PLATFORM_SRCS` et donc **compilé
une fois par moteur** : chaque Macintosh avait son propre « déjà armé » et aucun ne voyait celui
de l'autre. La réservation vit désormais dans `hal_circle.cpp` (`SHARED_SRCS`), le timer et son
interruption sont pris une fois pour la vie de la carte, et le gestionnaire est échangé — le
patron de l'unique réservation de la souris et de l'unique framebuffer.

Confirmé par le log, après un tour complet 68k → redémarrage → PowerPC :

```
okapia-board: Fine timer already claimed; handler swapped for the other engine
```

## Ce qui reste à mesurer

- **Sur le Pi 4**, avec une vraie souris USB : le `mousedpi` juste sera un troisième chiffre.
  Sur ce banc, un trackpad de MacBook Pro vu à travers macOS et QEMU mesure ~200 — la densité
  d'une souris d'époque, par coïncidence — et le confort a finalement été réglé à 400, qui est
  un choix de confort et non une description exacte.
- **L'écart de ressenti entre les deux moteurs** n'est pas un défaut d'étalonnage : les deux
  voient la même densité. Il vient de deux courbes différentes — les tables natives de 7.6 d'un
  côté, notre implémentation de la table du ROM de 1994 de l'autre.
- **`SCREEN_DPI` est figé à 72** dans notre code. Tout le résultat y est directement
  proportionnel, et le ROM en fait un champ (`CrsrDev.a:2232`) qu'un Système peut changer.
