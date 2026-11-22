//
// check_card.cpp — what a volume is as an Okapia card (okapia_card_check.cpp).
//
// The installer's rules (okapia-installer core/src/card.rs and log.rs): FAT
// told apart by cluster count, the build found in a kernel, the sessions of a
// log that may have wrapped, and a folder diagnosed whole.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_card_check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <initializer_list>

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s  %s\n", bOK ? "ok  " : "FAIL", pWhat);
    if (!bOK)
        s_nFailures++;
}

static void Write (const char *pDir, const char *pName, const char *pText)
{
    char Path[512];
    snprintf (Path, sizeof Path, "%s/%s", pDir, pName);
    FILE *f = fopen (Path, "wb");
    fputs (pText, f);
    fclose (f);
}

int main ()
{
    printf ("=== FAT by cluster count ===\n");
    Expect (CardFatKind (4084) == CardFsFat12 && CardFatKind (4085) == CardFsFat16,
            "4085 clusters and more are FAT16");
    Expect (CardFatKind (65524) == CardFsFat16 && CardFatKind (65525) == CardFsFat32,
            "65525 and more are FAT32");

    printf ("=== the build in a kernel ===\n");
    {
        char V[32];
        const char Kernel[] = "\x01\x02junk 1234567-abc Okapia 261121-8b8bf89+3d10 (rpi4)";
        Expect (CardFindVersion ((const unsigned char *) Kernel, sizeof Kernel - 1, V, sizeof V)
                && strcmp (V, "261121-8b8bf89+3d10") == 0, "YYMMDD-sha+xxxx is found, a longer number is not");
        const char Plain[] = "x 261122-0123abc y";
        Expect (CardFindVersion ((const unsigned char *) Plain, sizeof Plain - 1, V, sizeof V)
                && strcmp (V, "261122-0123abc") == 0, "and YYMMDD-sha alone");
        const char None[] = "26112-abcdef0 no";
        Expect (!CardFindVersion ((const unsigned char *) None, sizeof None - 1, V, sizeof V),
                "five digits are not a date");
    }

    printf ("=== the sessions of a log ===\n");
    {
        unsigned n;
        char V[32];
        bool bClean;
        CardLogSummary ("a: Okapia 261121-8b8bf89 (rpi4), built Nov\n"
                        "b: Initialising the emulator\n"
                        "c: Shut down cleanly, disk closed\n", &n, V, sizeof V, &bClean);
        Expect (n == 1 && strcmp (V, "261121-8b8bf89") == 0 && bClean, "a Macintosh shut down: clean");
        CardLogSummary ("a: Okapia 261121-8b8bf89 (rpi4), built Nov\n"
                        "b: Initialising the emulator\n", &n, V, sizeof V, &bClean);
        Expect (n == 1 && !bClean, "a Macintosh left running: switched off without Shut Down");
        CardLogSummary ("a: Okapia 261121-8b8bf89 (rpi4), built Nov\n"
                        "b: Powered off from the firmware\n", &n, V, sizeof V, &bClean);
        Expect (bClean, "the boot menu alone, then off: nothing to repair");
        CardLogSummary ("new: Okapia 261122-0123abc (rpi4), built Nov\n"
                        "new: Initialising the emulator\n"
                        "--- okapia.log is full and starts again here: newer lines are above this\n"
                        "old: Okapia 261121-8b8bf89 (rpi4), built Nov\n"
                        "old: Shut down cleanly, disk closed\n", &n, V, sizeof V, &bClean);
        Expect (n == 2 && strcmp (V, "261122-0123abc") == 0 && !bClean,
                "a wrapped log is read older half first: the newest session is the last");
    }

    printf ("=== a folder diagnosed ===\n");
    {
        char Dir[] = "/tmp/okapiacardXXXXXX";
        if (mkdtemp (Dir) == 0)
        {
            Expect (false, "a temporary folder");
            return 1;
        }
        TCardVolume Volume = {};
        Volume.Fs = CardFsFat32;
        Volume.nCluster = 16384;
        TCardDiagnosis D;
        CardDiagnose (Dir, &Volume, &D);
        Expect (D.bFatOK && D.bSmallClusters, "FAT32 with 16 KB clusters: it works, slower");
        Expect (!D.bKernel && strstr (D.Missing, "start4.elf") != 0, "no Okapia: kernel and boot files missing");
        Write (Dir, "kernel8-rpi4.img", "....Okapia 261121-8b8bf89+3d10 ....");
        for (const char *p : { "start4.elf", "fixup4.dat", "bcm2711-rpi-4-b.dtb", "armstub8-rpi4.bin", "config.txt" })
            Write (Dir, p, "x");
        Write (Dir, "okapia.log", "a: Okapia 261121-8b8bf89+3d10 (rpi4), built Nov\nb: Initialising the emulator\n");
        Volume.nCluster = 32768;
        CardDiagnose (Dir, &Volume, &D);
        Expect (D.bKernel && D.Missing[0] == '\0' && strcmp (D.Version, "261121-8b8bf89+3d10") == 0,
                "Okapia installed, its build read in the kernel");
        Expect (!D.bSmallClusters && D.bUncleanShutdown && D.nSessions == 1,
                "32 KB clusters; the last session left a Macintosh running");
        Volume.Fs = CardFsOther;
        CardDiagnose (Dir, &Volume, &D);
        Expect (!D.bFatOK, "exFAT: the Pi cannot start from it");
        for (const char *p : { "kernel8-rpi4.img", "start4.elf", "fixup4.dat", "bcm2711-rpi-4-b.dtb",
                               "armstub8-rpi4.bin", "config.txt", "okapia.log" })
        {
            char Path[512];
            snprintf (Path, sizeof Path, "%s/%s", Dir, p);
            unlink (Path);
        }
        rmdir (Dir);
    }

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
