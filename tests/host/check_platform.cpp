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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "hfs_volume_circle.h"
#include "mac_layout.h"

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

    // 256 Mo + 5 + 0,0625 + 0,5 : la valeur écrite dans le plan.
    Expect (L.nHostBytes == 256u * 1024 * 1024 + 0x500000 + 0x10000 + 0x80000 + 0x100000,
            "soit 262,6 Mo pour 256 Mo de RAM Mac");

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

    printf ("\n%u écart(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
