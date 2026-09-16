/*
 * card_circle.cpp — see card_circle.h.
 *
 * Moved out of src/kernel/kernel.cpp, where it ran on the 68k start path only.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "card_circle.h"
#include "hfs_volume_circle.h"
#include "prefs.h"

#include <stdio.h>
#include <string.h>

#ifdef SHEEPSHAVER
#define FROM "card-ppc"
#else
#define FROM "card"
#endif

/*
 *  What the card carries
 *
 *  A file name says nothing about whether a Mac can start from an image, and
 *  guessing from one is how a boot failure ends up looking like an emulator
 *  bug. libhfs answers exactly: a non-zero blessed CNID is the System folder
 *  the ROM itself will look for. Read-only throughout — this describes the
 *  volumes, it does not touch them.
 */

// Enough for any card worth booting from, and it costs 8 KB of stack-free BSS.
static const unsigned MAX_VOLUMES = 8;
static THfsVolumeInfo s_Volumes[MAX_VOLUMES];
static unsigned s_nVolumes = 0;

// The volume the Mac will start from: the first configured disk that is there.
// The emulator offers them to the ROM in the order disk.cpp reads them, so this
// is the one whose System decides the model id.
static char s_BootVolume[64] = "";

void CardInventory (void)
{
    // The scan itself always runs: the fallback boot volume and the clock below
    // both depend on it. The preference only decides whether it is listed.
    s_nVolumes = HfsInventory (s_Volumes, MAX_VOLUMES);

    if (s_nVolumes == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No HFS volume found on the card");
        return;
    }
    if (!PrefsFindBool ("hfsinventory"))
    {
        return;
    }

    for (unsigned i = 0; i < s_nVolumes; i++)
    {
        const THfsVolumeInfo *pInfo = &s_Volumes[i];
        CLogger::Get ()->Write (FROM, LogNotice,
                        "%s: \"%s\", %lu MB, %lu MB free, %lu files, %lu folders, %s, %s",
                        pInfo->Path, pInfo->Name,
                        pInfo->TotalKB / 1024, pInfo->FreeKB / 1024,
                        pInfo->NumFiles, pInfo->NumDirs,
                        pInfo->Blessed != 0 ? "bootable" : "no System folder",
                        pInfo->bClean ? "clean" : "in use");
    }
}

/*
 *  Take the clock forward to the last time this machine was used
 *
 *  The build time is a floor, not an answer: it says when the kernel was made,
 *  not when the Mac last ran. The volumes on the card know better — a Mac
 *  stamps drLsMod every time it writes, so the most recent one is roughly when
 *  the machine was last switched off. That is a much better guess, it follows
 *  the user's own use rather than the developer's, and it costs one field of an
 *  inventory that already runs.
 *
 *  It is a floor too, never a correction: the clock only ever moves forward
 *  here. A volume dated in the past is simply older evidence, and a volume
 *  dated where no Macintosh of this card could have been is damaged, not
 *  prescient — HfsDateIsPlausible() draws that line.
 *
 *  Real sources still come first when they exist: an RTC, then NTP
 *  (docs/topics/clock-and-pram.md). Both belong here, ahead of this, and
 *  nothing downstream will have to change.
 */

void CardRefineClock (void)
{
    // Same frame on both sides: drLsMod is local time, the build time is UTC.
    // Comparing them raw is how you end up an hour out, twice a year. The zone
    // is the timer's, which is the one it actually accepted.
    const long nOffset = (long) CTimer::Get ()->GetTimeZone () * 60;
    const long nBuild = (long) OKAPIA_BUILD_TIME + nOffset;
    long nBest = nBuild;
    const char *pFrom = 0;

    // And never behind the clock we are already keeping. This runs again after
    // a restart from Mac OS, minutes into the session, and a floor made only of
    // the build time would wind it back — which is precisely the drLsMod before
    // drCrDate that fsck_hfs reports as "MDB needs minor repair".
    const long nNow = (long) CTimer::Get ()->GetLocalTime ();
    if (nNow > nBest)
    {
        nBest = nNow;
    }

    for (unsigned i = 0; i < s_nVolumes; i++)
    {
        const long nWhen = s_Volumes[i].nLastModified;
        if (!HfsDateIsPlausible (nWhen, nBuild))
        {
            CLogger::Get ()->Write (FROM, LogWarning,
                            "%s: last-modified date is implausible (%ld), ignored",
                            s_Volumes[i].Path, nWhen);
            continue;
        }
        if (nWhen > nBest)
        {
            nBest = nWhen;
            pFrom = s_Volumes[i].Path;
        }
    }

    if (pFrom == 0)
    {
        return;                         // the build time already wins
    }

    // TRUE: nBest is local seconds, which is what SetTime stores.
    if (!CTimer::Get ()->SetTime ((unsigned) nBest, TRUE))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Could not move the clock forward");
        return;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Clock: %s, from %s — later than the build time",
                    (const char *) *CTimer::Get ()->GetTimeString (), pFrom);
}

/*
 *  Get the configured volumes into a state the Mac will accept
 *
 *  A Mac refuses to start from a volume whose MDB still says "in use", and that
 *  is exactly what an interrupted session leaves behind — on real hardware too.
 *  Repairing it is what a second bootable System would do; doing it ourselves
 *  is why a hard stop here costs the boot and not the data. It writes to the
 *  user's volume, so "hfsrepair false" turns it off and the Mac is then left to
 *  show the question-mark floppy, which is the honest alternative.
 */

// Does the inventory list this path as carrying a System folder? Unknown paths
// answer no: the inventory covers every HFS volume in the card's root.
static bool VolumeIsBootable (const char *pPath)
{
    for (unsigned i = 0; i < s_nVolumes; i++)
    {
        if (strcmp (s_Volumes[i].Path, pPath) == 0)
        {
            return s_Volumes[i].Blessed != 0;
        }
    }
    return false;
}

// Could this engine start the System on that volume? Asked of the fallback
// only: a configured disk was the chooser's decision, and the chooser already
// sent the card to the engine its System needs. An unreadable version is given
// the benefit of the doubt, as it was before the question was asked.
static bool VolumeSuitsEngine (const char *pPath)
{
    THfsSystemVersion Version;
    if (!HfsSystemVersion (pPath, &Version))
    {
        return true;
    }
    const THfsSystemFlavour Flavour = HfsFlavourOf (&Version);
#ifdef SHEEPSHAVER
    return Flavour != HfsFlavour68k;
#else
    return Flavour != HfsFlavourPowerPC;
#endif
}

// CDROMRefNum (cdrom.h:24), as the `bootdriver` preference states it. Not
// included from there: this file is built for both engines and each has its own
// cdrom.h, while the number is the Macintosh's and the same in both.
static const int BOOT_DRIVER_CDROM = -62;

// Is this path in the `cdrom` list? A volume can be in one list or the other,
// never both, and the fallback below has to know: adding a `disk` line for an
// image already opened as a CD-ROM would hand the same file to the Mac twice,
// through two drivers, which is a mounted-twice volume and not a spare drive.
static bool VolumeIsCdrom (const char *pPath)
{
    for (int i = 0; ; i++)
    {
        const char *pCd = PrefsFindString ("cdrom", i);
        if (pCd == 0)
        {
            return false;
        }
        if (strcmp (pCd, pPath) == 0)
        {
            return true;
        }
    }
}

bool CardPrepareVolumes (void)
{
    const bool bRepair = PrefsFindBool ("hfsrepair");
    unsigned nUsable = 0;
    char FirstPresent[sizeof s_BootVolume]  = "";
    char FirstBootable[sizeof s_BootVolume] = "";

    s_BootVolume[0] = '\0';

    for (int i = 0; ; i++)
    {
        const char *pDisk = PrefsFindString ("disk", i);
        if (pDisk == 0)
        {
            break;
        }
        if (*pDisk == '\0')
        {
            continue;
        }

        // A '*' prefix means read-only to the emulator (disk.cpp:161); the path
        // on the card starts after it.
        const char *pPath = (*pDisk == '*') ? pDisk + 1 : pDisk;

        FILE *pFile = fopen (pPath, "rb");
        if (pFile == 0)
        {
            CLogger::Get ()->Write (FROM, LogError, "disk %s: not on the card", pPath);
            continue;
        }
        fclose (pFile);
        if (nUsable++ == 0)
        {
            snprintf (FirstPresent, sizeof FirstPresent, "%s", pPath);
        }

        // The ROM does not start from the first drive, it starts from the first
        // one carrying a System — so a data volume listed ahead of the boot one
        // must not be mistaken for it. The inventory already knows which is
        // which, and getting this wrong would send the model id to the wrong
        // volume and run-test.sh to a volume the session never wrote.
        if (FirstBootable[0] == '\0' && VolumeIsBootable (pPath))
        {
            snprintf (FirstBootable, sizeof FirstBootable, "%s", pPath);
        }

        if (HfsInspect (pPath))
        {
            continue;
        }
        if (!bRepair)
        {
            CLogger::Get ()->Write (FROM, LogWarning,
                            "%s: left as it is (hfsrepair false); the Mac will refuse it",
                            pPath);
            continue;
        }
        HfsRepair (pPath);
    }

    // The CD-ROM drives are checked but never touched. CDROMInit() opens them
    // read-only whatever anybody asks (cdrom.cpp:324), so a repair could not
    // write to one, and offering to repair what cannot be written is the kind
    // of promise this firmware does not make.
    char FirstCd[sizeof s_BootVolume] = "";
    for (int i = 0; ; i++)
    {
        const char *pCd = PrefsFindString ("cdrom", i);
        if (pCd == 0)
        {
            break;
        }
        if (*pCd == '\0')
        {
            continue;
        }
        FILE *pFile = fopen (pCd, "rb");
        if (pFile == 0)
        {
            CLogger::Get ()->Write (FROM, LogError, "cdrom %s: not on the card", pCd);
            continue;
        }
        fclose (pFile);
        if (FirstCd[0] == '\0')
        {
            snprintf (FirstCd, sizeof FirstCd, "%s", pCd);
        }
        CLogger::Get ()->Write (FROM, LogNotice, "cdrom %s", pCd);
    }

    // A Macintosh told to start from the CD driver starts from the first disc
    // that driver offers, so that is the volume this session is about — and
    // naming it is not decoration: run-test.sh judges what this line names, and
    // the model id is read from its System. Answering with a disk here would
    // judge a volume the session never wrote. `bootdriver` is CDROMRefNum, -62
    // (cdrom.h:24), written into the parameter RAM at 0x7a by main.cpp:139 —
    // and SheepShaver reads it exactly the same way (SheepShaver/src/main.cpp:113).
    if (PrefsFindInt32 ("bootdriver") == BOOT_DRIVER_CDROM && FirstCd[0] != '\0')
    {
        snprintf (s_BootVolume, sizeof s_BootVolume, "%s", FirstCd);
        CLogger::Get ()->Write (FROM, LogNotice, "Boot volume: %s (CD-ROM)", s_BootVolume);
        return true;
    }

    if (nUsable > 0)
    {
        // Named explicitly because it is not deducible from the card: with two
        // images present, the one the Mac starts from is the first configured
        // one *that carries a System*. scripts/run-test.sh judges this volume,
        // and the model id is read from its System version, so naming the wrong
        // one is worse than naming none.
        if (FirstBootable[0] != '\0')
        {
            snprintf (s_BootVolume, sizeof s_BootVolume, "%s", FirstBootable);
        }
        else
        {
            // Nothing the inventory could open as a bootable volume. Say so
            // rather than pass the first drive off as the boot one.
            snprintf (s_BootVolume, sizeof s_BootVolume, "%s", FirstPresent);
            CLogger::Get ()->Write (FROM, LogWarning,
                            "No configured disk carries a System; assuming %s",
                            s_BootVolume);
        }
        CLogger::Get ()->Write (FROM, LogNotice, "Boot volume: %s", s_BootVolume);
        return true;
    }

    // Nothing the preferences name is actually there. The inventory already
    // knows which images on the card a Mac could start from, so say so and use
    // one rather than stopping at a question-mark floppy over a stale path.
    for (unsigned i = 0; i < s_nVolumes; i++)
    {
        if (   s_Volumes[i].Blessed == 0 || VolumeIsCdrom (s_Volumes[i].Path)
            || !VolumeSuitsEngine (s_Volumes[i].Path))
        {
            continue;
        }
        CLogger::Get ()->Write (FROM, LogWarning,
                        "No configured disk exists; falling back to %s (\"%s\")",
                        s_Volumes[i].Path, s_Volumes[i].Name);
        CLogger::Get ()->Write (FROM, LogNotice, "Boot volume: %s", s_Volumes[i].Path);
        PrefsAddString ("disk", s_Volumes[i].Path);
        snprintf (s_BootVolume, sizeof s_BootVolume, "%s", s_Volumes[i].Path);
        if (!s_Volumes[i].bClean && bRepair)
        {
            HfsRepair (s_Volumes[i].Path);
        }
        return true;
    }

    CLogger::Get ()->Write (FROM, LogError, "No bootable volume on the card");
    return false;
}

const char *CardBootVolume (void)
{
    return s_BootVolume;
}

// It exists so a test can judge the volume in the state the repair leaves it:
// booting the Mac would remount it and mark it in use again within a couple of
// seconds, which is the honest behaviour but hides what we want to measure.
// scripts/run-test.sh uses it.
bool CardRepairOnly (void)
{
    FILE *pMarker = fopen ("/repair-only", "r");
    if (pMarker == 0)
    {
        return false;
    }
    fclose (pMarker);
    CLogger::Get ()->Write (FROM, LogNotice, "Repair-only card: stopping here");
    return true;
}
