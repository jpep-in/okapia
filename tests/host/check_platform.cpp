/*
 * check_platform.cpp — the platform answers nobody sees going wrong.
 *
 * All three are decidable without a card, an emulator or a screen, and all
 * three fail quietly: a preferences line that disappears is noticed the day the
 * setting was needed, a System read as the wrong sort after the boot menu has
 * offered the wrong emulator, and a memory plan that is out by a megabyte gives
 * a Macintosh reading someone else's bytes rather than an error.
 *
 * The preferences half runs against the *real* keyword tables — upstream's
 * common_prefs_items and our platform_prefs_items are linked in, not mocked —
 * so a keyword added to either is covered by these checks the day it is added.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "hfs_volume_circle.h"
#include "mac_layout.h"
#include "mac_ram_circle.h"
#include "compositor_circle.h"

// prefs_circle.cpp. Declared here rather than in a header of its own: it has
// exactly one caller in the kernel, SavePrefs(), a few lines below it.
extern unsigned PrefsCollectUnknown (const char *pPath, char *pBuffer, size_t nSize,
                                     unsigned *pnKept);

// prefs_items.cpp calls it from AddPrefsDefaults(); the serial ports are not
// what is under test.
void SysAddSerialPrefs (void) {}

// hfs_volume_circle.cpp reaches the card through these. Nothing below mounts a
// volume — the flavour rule is asked about a version, not about an image — so
// they only have to exist.
size_t Sys_read (void *fh, void *buffer, long long offset, size_t length)
{
    return pread ((int) (long) fh, buffer, length, (off_t) offset);
}
void *Sys_open (const char *pName, bool, bool)
{
    int nFD = open (pName, O_RDONLY);
    return nFD < 0 ? 0 : (void *) (long) nFD;
}
void Sys_close (void *fh) { close ((int) (long) fh); }

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "ECHEC", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

/*
 *  Preferences: the lines this build does not understand
 */

static const char TMP[] = "check_platform.tmp";

static void Write (const char *pContents)
{
    FILE *f = fopen (TMP, "w");
    fputs (pContents, f);
    fclose (f);
}

static void CheckUnknownLines (void)
{
    printf ("\nles lignes de préférences non déclarées\n");

    char     Buffer[512];
    unsigned nKept  = 0;
    unsigned nFound = 0;

    // "ramsize" is upstream's, "hfsrepair" is ours: both tables are consulted,
    // and a line is only kept when neither claims its keyword.
    Write ("ramsize 268435456\n"
           "hfsrepair true\n"
           "gfxaccel true\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 1 && nKept == 1, "une seule ligne sur trois est inconnue");
    Expect (strcmp (Buffer, "gfxaccel true\n") == 0,
            "et elle est rendue entière, valeur comprise");

    // A value with spaces in it, which is the case a keyword-and-value split
    // gets wrong: everything after the keyword belongs to the value.
    Write ("machine /Mac OS 9.image | sheepshaver | /macosrom\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nKept == 1
            && strcmp (Buffer, "machine /Mac OS 9.image | sheepshaver | /macosrom\n") == 0,
            "les espaces d'une valeur sont conservés");

    // The header SavePrefs() writes is regenerated at every save, so keeping
    // its comments would double the file line by line.
    Write ("# un commentaire\n"
           "; un autre\n"
           "\n"
           "   \n"
           "ramsize 42\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 0 && nKept == 0,
            "commentaires, lignes vides et lignes blanches sont ignorés");

    // Nothing may be lost quietly: what does not fit is still counted, and
    // SavePrefs() warns on the difference.
    Write ("aaaa 1\nbbbb 2\ncccc 3\n");
    nFound = PrefsCollectUnknown (TMP, Buffer, 10, &nKept);
    Expect (nFound == 3, "les lignes qui ne tiennent pas sont comptées");
    Expect (nKept < nFound, "et ne sont pas écrites");
    Expect (strlen (Buffer) < 10, "le tampon n'est jamais dépassé");

    // A save on a card that has no preferences file yet, which is the first
    // boot: SavePrefs() calls this before creating the file.
    unlink (TMP);
    nFound = PrefsCollectUnknown (TMP, Buffer, sizeof Buffer, &nKept);
    Expect (nFound == 0 && nKept == 0 && Buffer[0] == '\0',
            "un fichier absent ne rapporte rien");
}

/*
 *  Which emulator can boot this System
 */

static THfsSystemFlavour Flavour (unsigned nMajor, unsigned nMinor, unsigned nBugfix,
                                  bool bNative)
{
    THfsSystemVersion Version;
    memset (&Version, 0, sizeof Version);
    Version.nMajor      = nMajor;
    Version.nMinor      = nMinor;
    Version.nBugfix     = nBugfix;
    Version.bNativeCode = bNative;
    return HfsFlavourOf (&Version);
}

static void CheckFlavour (void)
{
    printf ("\nla nature du Système\n");

    // The two volumes staged in qemu/sd-contents/, measured 2026-09-04: these
    // two lines are the ones that have a right answer outside this file.
    Expect (Flavour (7, 1, 2, false) == HfsFlavour68k,
            "7.1.2 sans fragment natif : 68k, et rien à demander");
    Expect (Flavour (7, 6, 1, true) == HfsFlavourUniversal,
            "7.6.1 avec fragments natifs : universel, donc on demande");

    // The lower bound is SheepShaver's earliest correctives, 7.5.2. Below it,
    // a native fragment changes nothing: no emulator would take the volume.
    Expect (Flavour (7, 5, 0, true) == HfsFlavour68k,
            "7.5 est encore en dessous, fragments ou pas");
    Expect (Flavour (7, 5, 2, true) == HfsFlavourUniversal,
            "7.5.2 est la première version que les deux couvrent");

    // The upper bound is where Basilisk's own patch list stops.
    Expect (Flavour (8, 1, 0, true) == HfsFlavourUniversal,
            "8.1 est la dernière que les deux couvrent");
    Expect (Flavour (8, 5, 0, true) == HfsFlavourPowerPC,
            "8.5 est PowerPC, et rien à demander");
    Expect (Flavour (9, 0, 4, true) == HfsFlavourPowerPC,
            "9.0.4 aussi");

    // Deliberately not "PowerPC": Mac OS stayed largely 68k code inside, so a
    // fragment says the PowerPC half is installed and never that it is alone.
    Expect (Flavour (8, 0, 0, false) == HfsFlavour68k,
            "dans la bande, sans fragment natif, c'est du 68k");

    // System 6 has no 'vers' in its System file, so HfsSystemVersion() fails
    // and leaves the structure zeroed. Answering "68k" there would be right by
    // accident; the rule has to say it does not know.
    Expect (Flavour (0, 0, 0, false) == HfsFlavourUnreadable,
            "pas de version lisible : la règle ne conclut pas");
}

/*
 *  Where SheepShaver's Macintosh lands in memory
 */

static void CheckLayout (void)
{
    printf ("\nle plan mémoire de SheepShaver\n");

    TMacLayout L;
    Expect (MacLayoutPlan (256 * 1024 * 1024, &L), "256 Mo tiennent");

    Expect (L.nRAMBase == 0x10000000, "la RAM commence là où l'amont la met");
    Expect (L.nROMBase == L.nRAMBase + L.nRAMSize,
            "la ROM suit la RAM immédiatement — c'est tout le §19.4");
    Expect ((L.nROMBase & 0xFFFFF) == 0, "et sur une frontière de mégaoctet");
    Expect (L.nSigStack == L.nROMBase + OKAPIA_ROM_AREA_SIZE,
            "la pile de signal suit la zone ROM, cinq mégaoctets et non quatre");
    Expect (L.nSheepBase == L.nSigStack + OKAPIA_SIG_STACK_SIZE,
            "puis le bloc propre à SheepShaver");
    Expect (L.nFrameBase == L.nSheepBase + OKAPIA_SHEEP_SIZE,
            "et l'écran, qui doit être à une adresse Mac comme le reste");
    Expect (L.nHostBytes == (size_t) (L.nEnd - L.nRAMBase),
            "le bloc hôte couvre exactement l'invité, sans trou");

    // 256 Mo + 5 (ROM) + 0,0625 (pile) + 0,5 (SheepMem) + 4 (écran).
    Expect (L.nHostBytes == 256u * 1024 * 1024 + OKAPIA_ROM_AREA_SIZE
                            + OKAPIA_SIG_STACK_SIZE + OKAPIA_SHEEP_SIZE + OKAPIA_FRAME_SIZE,
            "soit 265,6 Mo pour 256 Mo de RAM Mac");
    // Et le supplément que mac_ram_circle.h réclame pour les deux moteurs doit
    // couvrir cette disposition, sinon le second moteur redemande un bloc.
    Expect (L.nHostBytes - L.nRAMSize <= OKAPIA_MAC_BLOCK_OVERHEAD,
            "le supplément partagé couvre la disposition la plus gourmande");

    // Un alignement qui ne fait rien quand il n'a rien à faire.
    TMacLayout M;
    Expect (MacLayoutPlan (255 * 1024 * 1024 + 1, &M)
            && M.nROMBase == 0x10000000 + 256u * 1024 * 1024,
            "une taille non alignée pousse la ROM au mégaoctet suivant");

    // Les trois refus. Ils disent non au démarrage, ce qui est le seul moment
    // où un plan mémoire faux peut encore être expliqué.
    Expect (!MacLayoutPlan (0, &M), "zéro octet de RAM est refusé");
    Expect (!MacLayoutPlan (0xF0000000u, &M),
            "une taille qui ferait déborder l'espace 32 bits aussi");

    // La Kernel Data est à 0x68ffe000 et c'est la ROM qui la place, pas nous :
    // le bloc doit finir en dessous, pas seulement commencer en dessous.
    Expect (!MacLayoutPlan (0x58ffe000u, &M),
            "et une RAM qui atteindrait la Kernel Data de la ROM");
    Expect (MacLayoutPlan (0x58000000u, &M) && M.nEnd < 0x68ffe000u,
            "juste en dessous, cela passe encore");
}

int main (void)
{
    CheckUnknownLines ();
    CheckFlavour ();
    CheckLayout ();
    unlink (TMP);

    /*
     *  Les tuiles annoncées
     *
     *  video_set_dirty_area() est la seule chose que le moteur PowerPC a et que
     *  le 68k n'a pas : le Macintosh dit ce qu'il a changé. L'arithmétique des
     *  tuiles est la partie qui peut se tromper en silence — une tuile oubliée
     *  laisse un morceau d'écran périmé, ce qui ressemble à du déchirement et
     *  pas à un calcul faux.
     */
    /*
     *  Les modes directs
     *
     *  Le Mac est gros-boutien et son pixel 16 bits est 0RRRRRGG GGGBBBBB, ce
     *  que les blitters directs de video_blit.cpp ne supposent pas. Leur avoir
     *  confié un mode direct a donné un écran entièrement d'une seule couleur en
     *  16 bits et des bandes verticales en 32 — une faute qui se voit tout de
     *  suite à l'écran et jamais dans un lien. Ici on la verrait avant.
     */
    printf ("\n  Les modes directs du Macintosh\n");

    unsigned char Src[8];
    unsigned Out[4];

    // 16 bits, gros-boutien : rouge pur, vert pur, bleu pur.
    Src[0] = 0x7C; Src[1] = 0x00;   // 0x7C00
    Src[2] = 0x03; Src[3] = 0xE0;   // 0x03E0
    Src[4] = 0x00; Src[5] = 0x1F;   // 0x001F
    CompositorConvert16To32 ((u8 *) Out, Src, 6);
    Expect (Out[0] == 0xFF0000FFu, "16 bits : le rouge du Mac sort en octet bas");
    Expect (Out[1] == 0xFF00FF00u, "16 bits : le vert au milieu");
    Expect (Out[2] == 0xFFFF0000u, "16 bits : le bleu en haut");

    // 32 bits : le Mac écrit xRGB, l'octet de tête est ignoré.
    Src[0] = 0x00; Src[1] = 0x12; Src[2] = 0x34; Src[3] = 0x56;
    CompositorConvert32To32 ((u8 *) Out, Src, 4);
    Expect (Out[0] == 0xFF563412u, "32 bits : xRGB devient BGRA, l'octet de tête jeté");

    printf ("\n  Les régions annoncées par le Macintosh\n");

    TCompositor C;
    memset (&C, 0, sizeof C);
    C.nWidth  = 640;
    C.nHeight = 480;

    CompositorAnnounce (&C, 0, 0, 1, 1);
    Expect (C.Announced[0] == 1, "un pixel en haut à gauche marque la première tuile");
    unsigned nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 1, "et elle seule");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 0, 0, 640, 480);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 256, "tout l'écran marque les 256 tuiles");

    // 640/16 = 40 pixels par tuile, 480/16 = 30. Un rectangle à cheval sur la
    // frontière doit marquer les deux, pas une.
    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 39, 0, 2, 1);
    Expect (C.Announced[0] == 0x3, "un rectangle à cheval marque les deux tuiles");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 600, 450, 200, 200);
    Expect (C.Announced[15] == 0x8000, "ce qui déborde est rogné, pas replié");

    memset (C.Announced, 0, sizeof C.Announced);
    CompositorAnnounce (&C, 700, 0, 10, 10);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 0, "entièrement hors écran ne marque rien");

    CompositorAnnounce (&C, 0, 0, -5, 10);
    CompositorAnnounce (&C, 0, 0, 10, 0);
    nSet = 0;
    for (unsigned i = 0; i < 16; i++) nSet += (unsigned) __builtin_popcount (C.Announced[i]);
    Expect (nSet == 0, "une largeur ou une hauteur nulle ou négative non plus");

    /*
     *  Ce que le compositeur écrit à l'écran
     *
     *  Le framebuffer du Pi est de la mémoire Device : un accès non aligné y
     *  est une faute, et un Macintosh en 912x492 a tué la carte parce que ses
     *  tuiles de 57 pixels mettaient la copie vectorisée hors alignement. Ici
     *  la faute ne se voit pas — l'hôte accepte tout — mais le chemin réécrit
     *  pour l'éviter doit dessiner exactement la même chose, aux largeurs
     *  impaires, aux origines impaires et avec un pitch qui n'est pas un
     *  multiple de 16. C'est ce qu'on vérifie, pixel par pixel.
     */
    printf ("\n  Ce que le compositeur écrit à l'écran\n");
    {
        struct { unsigned w, h, ow, oh, scale; const char *pWhat; } Cases[] = {
            {  912, 492, 1824,  984, 2, "912x492 doublé dans 1824x984, le mode qui tuait la carte" },
            {  853, 480, 2560, 1440, 3, "853x480 triplé dans 2560x1440" },
            { 1280, 720, 1824,  984, 1, "1280x720 à l'échelle 1, centré" },
            {  640, 480, 1366,  768, 1, "640x480 dans 1366x768 : origine impaire, pitch non multiple de 16" },
        };
        for (unsigned c = 0; c < sizeof Cases / sizeof Cases[0]; c++)
        {
            const unsigned w = Cases[c].w, h = Cases[c].h;
            const unsigned ow = Cases[c].ow, oh = Cases[c].oh;
            u8 *pMac    = (u8 *) calloc ((size_t) w * h, 4);
            u8 *pShadow = (u8 *) calloc ((size_t) w * h, 4);
            u8 *pOut    = (u8 *) aligned_alloc (16, (size_t) ow * oh * 4);
            memset (pOut, 0xEE, (size_t) ow * oh * 4);
            for (unsigned y = 0; y < h; y++)
            {
                for (unsigned x = 0; x < w; x++)
                {
                    u8 *p = pMac + (y * w + x) * 4;
                    p[0] = 0; p[1] = (u8) x; p[2] = (u8) y; p[3] = (u8) ((x >> 8) + (y >> 8) * 16);
                }
            }

            TCompositor K;
            memset (&K, 0, sizeof K);
            K.pSource = pMac; K.nWidth = w; K.nHeight = h;
            K.nBytesPerRow = w * 4; K.nSourceBits = 32;
            K.pOutput = pOut; K.nOutputWidth = ow; K.nOutputHeight = oh;
            K.nOutputPitch = ow * 4; K.nOutputBits = 32;
            K.pConvertRow = CompositorConvert32To32Bgr;
            K.pShadow = pShadow; K.nShadowBytes = w * h * 4;

            bool bOK = CompositorPlan (&K) && K.nScale == Cases[c].scale;
            if (bOK)
            {
                CompositorRun (&K);
            }
            for (unsigned y = 0; bOK && y < oh; y++)
            {
                for (unsigned x = 0; bOK && x < ow; x++)
                {
                    unsigned v;
                    memcpy (&v, pOut + (y * ow + x) * 4, 4);
                    const bool bInside = x >= K.nOriginX && x < K.nOriginX + w * K.nScale
                                      && y >= K.nOriginY && y < K.nOriginY + h * K.nScale;
                    unsigned nWant = 0;
                    if (bInside)
                    {
                        const u8 *p = pMac + (((y - K.nOriginY) / K.nScale) * w
                                              + (x - K.nOriginX) / K.nScale) * 4;
                        nWant = 0xFF000000u | (unsigned) p[1] << 16 | (unsigned) p[2] << 8 | p[3];
                    }
                    bOK = v == nWant;
                }
            }
            Expect (bOK, Cases[c].pWhat);
            free (pMac); free (pShadow); free (pOut);
        }
    }

    // Un volume fabriqué de bout en bout, ici, sans carte : hfs_format() écrit
    // sur un fichier qui a déjà sa taille, donc le test le fabrique comme le
    // fera FatFs et vérifie ensuite ce que le Macintosh lirait. Un formatage
    // qui écrit quelque chose que le montage suivant ne relit pas est un
    // volume qu'on découvre cassé plus tard, avec des fichiers dessus.
    printf ("\nfabriquer un volume\n");
    {
        const char *pPath = "/tmp/okapia-check-newvolume.image";
        const unsigned long nBytes = 20UL * 1024 * 1024;

        unlink (pPath);
        int nFD = open (pPath, O_RDWR | O_CREAT | O_TRUNC, 0600);
        Expect (nFD >= 0 && ftruncate (nFD, (off_t) nBytes) == 0,
                "un fichier de 20 Mo, comme FatFs le fera sur la carte");
        if (nFD >= 0)
        {
            close (nFD);
        }

        Expect (HfsFormat (pPath, "Macintosh HD"), "hfs_format() l'accepte");

        THfsVolumeInfo Info;
        Expect (HfsDescribe (pPath, &Info), "et il se relit comme un volume");
        Expect (strcmp (Info.Name, "Macintosh HD") == 0, "sous le nom demandé");
        Expect (Info.Blessed == 0, "vide, donc sans dossier système : il ne démarre pas");
        Expect (Info.bClean, "et propre, puisque personne ne l'a encore monté");
        // Ce que le Mac verra vraiment : HFS standard réserve ses tables, donc
        // le libre est un peu sous la taille demandée, jamais au-dessus.
        Expect (Info.TotalKB <= nBytes / 1024 && Info.TotalKB > nBytes / 1024 - 512,
                "de la taille du fichier, aux tables près");
        Expect (Info.FreeKB <= Info.TotalKB, "et il ne s'invente pas de place");

        // Les noms que libhfs refuse. Le bouton Créer les refuse avant, mais
        // c'est ici que la règle est écrite, et une règle en deux endroits est
        // une règle qui divergera.
        Expect (!HfsFormat (pPath, ""), "un nom vide est refusé");
        Expect (!HfsFormat (pPath, "Disque:2"), "un nom avec deux-points aussi");
        Expect (!HfsFormat (pPath, "un nom de plus de vingt-sept caracteres"),
                "et un nom trop long");
        // Refusés, et le volume d'avant est toujours là.
        Expect (HfsDescribe (pPath, &Info) && strcmp (Info.Name, "Macintosh HD") == 0,
                "et un refus ne détruit pas le volume qui était là");
        unlink (pPath);
    }

    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
