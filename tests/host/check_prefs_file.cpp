// check_prefs_file.cpp — Okapia_Prefs on the card, written so that a stop at any
// moment leaves the old settings or the new ones.
//
// SavePrefs() writes Okapia_Prefs.new, syncs it, and renames it over the old
// file (removing the old one first where rename will not replace, as FatFs
// will not). LoadPrefs() reads the new file when the old one is missing or
// empty. The stops in between are reproduced here by leaving the files as
// they would be found: prefs_okapia.cpp is compiled as the desktop compiles
// it (OKAPIA_HOSTED), with the card in a temporary folder, and its rename()
// is this file's, which refuses to replace a file when asked to, as FatFs does.
// After a stop, the next load finishes the interrupted save
// (CardPrefsRecover()), so the following save keeps the lines it does not know.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sysdeps.h"
#include "prefs.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char s_Card[256];
static int  s_nFailures;
static bool s_bFatFs;           // rename() refuses to replace, as on the board

extern "C" int OkapiaTestRename (const char *pFrom, const char *pTo)
{
    struct stat Info;
    if (s_bFatFs && stat (pTo, &Info) == 0)
    {
        errno = EEXIST;
        return -1;
    }
    return rename (pFrom, pTo);
}

// The desktop syncs through its own policy (sync_desktop.cpp); a folder on the
// host's disk needs nothing more for this check.
bool CardSync (const char *pPath)
{
    return true;
}

// The desktop's CardPath(): a card path, under the card's folder.
const char *CardPath (const char *pPath)
{
    static char Ring[4][512];
    static unsigned n;
    char *p = Ring[n++ % 4];
    snprintf (p, sizeof Ring[0], "%s%s", s_Card, pPath);
    return p;
}

// prefs_items.cpp asks the platform for its serial ports' defaults; the
// board's answer lives in a file this check does not need.
void SysAddSerialPrefs (void)
{
}

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL", pWhat);
    if (!bOK)
        s_nFailures++;
}

static long FileSize (const char *pName)
{
    struct stat Info;
    return stat (CardPath (pName), &Info) == 0 ? (long) Info.st_size : -1;
}

static int32 RamAfterLoad (void)
{
    int argc = 0;
    char **argv = 0;
    PrefsInit (0, argc, argv);
    const int32 nRam = PrefsFindInt32 ("ramsize");
    PrefsExit ();
    return nRam;
}

static void Save (int32 nRam)
{
    int argc = 0;
    char **argv = 0;
    PrefsInit (0, argc, argv);
    PrefsReplaceInt32 ("ramsize", nRam);
    SavePrefs ();
    PrefsExit ();
}

int main (void)
{
    snprintf (s_Card, sizeof s_Card, "/tmp/okapia-prefs-XXXXXX");
    if (mkdtemp (s_Card) == 0)
    {
        perror ("mkdtemp");
        return 1;
    }

    // A card with no preferences gets the defaults, written whole.
    RamAfterLoad ();
    Expect (FileSize ("/Okapia_Prefs") > 0 && FileSize ("/Okapia_Prefs.new") < 0,
            "a card without preferences gets the file, and no .new is left");

    Save (96 << 20);
    Expect (RamAfterLoad () == (96 << 20) && FileSize ("/Okapia_Prefs.new") < 0,
            "a setting saved is read back, and the .new file is renamed away");

    // A stop after the old file was removed and before the rename (FatFs's
    // order): only the new file is there, whole.
    rename (CardPath ("/Okapia_Prefs"), CardPath ("/Okapia_Prefs.new"));
    Expect (RamAfterLoad () == (96 << 20),
            "with only Okapia_Prefs.new on the card, the settings are read from it");

    // An empty file, as an older Okapia left it when stopped while truncating
    // it in place, beside a whole new one.
    rename (CardPath ("/Okapia_Prefs"), CardPath ("/Okapia_Prefs.new"));
    FILE *pEmpty = fopen (CardPath ("/Okapia_Prefs"), "w");
    if (pEmpty != 0)
        fclose (pEmpty);
    Expect (FileSize ("/Okapia_Prefs") == 0 && RamAfterLoad () == (96 << 20),
            "an empty Okapia_Prefs beside a whole .new reads the .new");

    // Saving again leaves one whole file, whatever was found.
    Save (128 << 20);
    Expect (RamAfterLoad () == (128 << 20) && FileSize ("/Okapia_Prefs") > 0
            && FileSize ("/Okapia_Prefs.new") < 0,
            "the next save leaves one whole Okapia_Prefs and nothing beside it");

    // The board's order: FatFs will not rename over a file, so the old one is
    // removed first.
    s_bFatFs = true;
    Save (160 << 20);
    Expect (RamAfterLoad () == (160 << 20) && FileSize ("/Okapia_Prefs.new") < 0,
            "where rename will not replace, the old file is removed and the new one renamed");

    // A line this build does not know, kept by a stop in the board's window:
    // the load finishes the save, and the next save keeps the line.
    FILE *pAdd = fopen (CardPath ("/Okapia_Prefs"), "a");
    if (pAdd != 0)
    {
        fputs ("okapiafuturekeyword 42\n", pAdd);
        fclose (pAdd);
    }
    rename (CardPath ("/Okapia_Prefs"), CardPath ("/Okapia_Prefs.new"));
    Expect (RamAfterLoad () == (160 << 20) && FileSize ("/Okapia_Prefs") > 0
            && FileSize ("/Okapia_Prefs.new") < 0,
            "a load after the stop renames the new file into place");
    Save (192 << 20);
    char Text[16384] = "";
    FILE *pRead = fopen (CardPath ("/Okapia_Prefs"), "r");
    if (pRead != 0)
    {
        Text[fread (Text, 1, sizeof Text - 1, pRead)] = '\0';
        fclose (pRead);
    }
    Expect (RamAfterLoad () == (192 << 20) && strstr (Text, "okapiafuturekeyword 42") != 0,
            "the save after it keeps the line it does not know");

    unlink (CardPath ("/Okapia_Prefs"));
    unlink (CardPath ("/Okapia_Prefs.new"));
    rmdir (s_Card);

    printf (s_nFailures == 0 ? "all prefs file checks passed\n" : "%d prefs file check(s) failed\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
