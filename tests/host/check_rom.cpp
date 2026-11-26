//
// check_rom.cpp — what a ROM file is, and which disks the ROMs start.
//
// The installer's cases (okapia-installer core/src/rom.rs), on bytes built
// here, and its disk verdicts (verdict_for), on every flavour against every
// pair of ROMs; then the ROMs of qemu/sd-contents, when the tree has them.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s  %s\n", bOK ? "ok  " : "FAIL", pWhat);
    if (!bOK)
        s_nFailures++;
}

// A 68k ROM of this version word and size, its checksum right unless damaged.
static std::vector<unsigned char> Make68k (unsigned nVersion, size_t nSize, bool bDamaged = false)
{
    std::vector<unsigned char> D (nSize, 0);
    for (size_t i = 16; i < nSize; i++)
        D[i] = (unsigned char) (i * 7 + 3);
    D[8] = (unsigned char) (nVersion >> 8);
    D[9] = (unsigned char) nVersion;
    unsigned nSum = 0;
    for (size_t i = 4; i + 1 < nSize; i += 2)
        nSum += (unsigned) D[i] << 8 | D[i + 1];
    const unsigned nStored = bDamaged ? nSum + 1 : nSum;
    D[0] = (unsigned char) (nStored >> 24);
    D[1] = (unsigned char) (nStored >> 16);
    D[2] = (unsigned char) (nStored >> 8);
    D[3] = (unsigned char) nStored;
    return D;
}

static TRomFacts Facts (const std::vector<unsigned char> &D)
{
    TRomFacts F;
    RomIdentifyBytes (D.data (), D.size (), &F);
    return F;
}

int main ()
{
    printf ("=== ROMs ===\n");
    {
        const TRomFacts F = Facts (Make68k (0x067C, 1024 * 1024));
        Expect (F.Kind == Rom68k && F.Verdict == RomUsable && !F.bRecommended,
                "a 32-bit clean ROM is usable, and not the Quadra's by its checksum");
        Expect (F.nFirstSystem == 701 && F.nLastSystem == 810, "a 68k ROM starts System 7.0.1 to Mac OS 8.1");
    }
    {
        const TRomFacts F = Facts (Make68k (0x0178, 256 * 1024));
        Expect (F.Kind == RomUnusable && F.Verdict == RomNot32BitClean, "a Mac II ROM is refused, not 32-bit clean");
    }
    {
        const TRomFacts F = Facts (Make68k (0x067C, 512 * 1024, true));
        Expect (F.Kind == RomUnusable && F.Verdict == RomChecksumMismatch, "a damaged ROM is refused by its checksum");
    }
    {
        std::vector<unsigned char> D (4 * 1024 * 1024, 0);
        memcpy (D.data () + 0x30D064, "Boot Gazelle", 12);
        const TRomFacts F = Facts (D);
        Expect (F.Kind == RomPowerPCOldWorld && F.Verdict == RomUsable && F.nFirstSystem == 752,
                "a 4 MB ROM is read for its nanokernel: Gazelle, Mac OS 7.5.2 and later");
        Expect (strcmp (F.Detail, "Gazelle") == 0, "and named by it");
        memcpy (D.data () + 0x30D064, "Boot Mystery", 12);
        Expect (Facts (D).Verdict == RomUnknownNanokernel, "an unknown nanokernel is refused");
    }
    {
        std::vector<unsigned char> D (2 * 1024 * 1024, 0);
        memcpy (D.data (), "<CHRP-BOOT>", 11);
        const TRomFacts F = Facts (D);
        Expect (F.Kind == RomPowerPCNewWorld && F.nFirstSystem == 810,
                "a <CHRP-BOOT> file is New World: Mac OS 8.1 and later");
    }
    {
        std::vector<unsigned char> D (8, 0);
        Expect (Facts (D).Verdict == RomTooSmall, "eight bytes are not a ROM");
    }

    printf ("=== disks against the ROMs ===\n");
    TRomFacts M68k = Facts (Make68k (0x067C, 1024 * 1024));
    std::vector<unsigned char> Old (4 * 1024 * 1024, 0);
    memcpy (Old.data () + 0x30D064, "Boot TNT", 8);
    TRomFacts OldWorld = Facts (Old);
    std::vector<unsigned char> New (1024 * 1024, 0);
    memcpy (New.data (), "<CHRP-BOOT>", 11);
    TRomFacts NewWorld = Facts (New);

    Expect (DiskJudge (false, false, 0, FlavourUnreadable, &M68k, &OldWorld).Verdict == DiskData,
            "not HFS: a data disk");
    Expect (DiskJudge (true, false, 0, FlavourUnreadable, &M68k, 0).Verdict == DiskNoSystem,
            "HFS without a System Folder: a data disk");
    Expect (DiskJudge (true, true, 0, FlavourUnreadable, &M68k, 0).Verdict == DiskUnreadable,
            "a System whose version cannot be read");
    Expect (DiskJudge (true, true, 922, FlavourPowerPC, &M68k, &OldWorld).Verdict == DiskTooNew,
            "Mac OS 9.2.2 is later than Okapia starts");
    Expect (DiskJudge (true, true, 712, Flavour68k, &M68k, 0).Verdict == DiskStarts68k,
            "System 7.1.2 starts with the 68k ROM");
    {
        const TDiskJudgement J = DiskJudge (true, true, 712, Flavour68k, 0, &OldWorld);
        Expect (J.Verdict == DiskNeeds68k && J.bOtherPowerPCUseless,
                "and without it needs one, the PowerPC ROM of no use to it");
    }
    Expect (DiskJudge (true, true, 860, FlavourPowerPC, 0, &OldWorld).Verdict == DiskStartsPowerPC,
            "Mac OS 8.6 starts with an Old World ROM");
    {
        const TDiskJudgement J = DiskJudge (true, true, 860, FlavourPowerPC, &M68k, 0);
        Expect (J.Verdict == DiskNeedsPowerPC && J.bOther68kUseless,
                "and without one needs it, the 68k ROM of no use to it");
    }
    Expect (DiskJudge (true, true, 761, FlavourUniversal, &M68k, &OldWorld).Verdict == DiskStartsBoth,
            "7.6.1, universal, starts with either: the boot menu chooses");
    {
        const TDiskJudgement J = DiskJudge (true, true, 761, FlavourUniversal, 0, &NewWorld);
        Expect (J.Verdict == DiskPowerPCTooOld && J.nPowerPCFloor == 810,
                "a New World ROM does not start 7.6.1: it needs 8.1 or later");
    }
    Expect (DiskJudge (true, true, 761, FlavourUniversal, &M68k, &NewWorld).Verdict == DiskStarts68k,
            "but the 68k ROM beside it does");
    Expect (DiskJudge (true, true, 761, FlavourUniversal, 0, 0).Verdict == DiskNeedsAny,
            "with no ROM at all it needs one or the other");

    printf ("=== the ROMs of qemu/sd-contents, when present ===\n");
    static const struct { const char *pFile; TRomKind Kind; } Real[] =
    {
        { "../../qemu/sd-contents/okapia.rom", Rom68k },
        { "../../qemu/sd-contents/macIIci.rom", Rom68k },
        { "../../qemu/sd-contents/macse.rom", RomUnusable },
        { "../../qemu/sd-contents/powermac9600v1.rom", RomPowerPCOldWorld },
        { "../../qemu/sd-contents/macosrom16.rom", RomPowerPCNewWorld },
    };
    for (const auto &R : Real)
    {
        TRomFacts F;
        if (!RomIdentify (R.pFile, &F))
        {
            printf ("  skip  %s (not in this tree)\n", R.pFile + 6);
            continue;
        }
        char What[160];
        snprintf (What, sizeof What, "%s: %s", R.pFile + 22, F.Identity);
        Expect (F.Kind == R.Kind, What);
    }

    printf ("\n%u failure(s)\n", s_nFailures);
    return s_nFailures == 0 ? 0 : 1;
}
