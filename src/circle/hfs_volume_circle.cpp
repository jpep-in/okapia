/*
 * hfs_volume_circle.cpp — look at the volume before handing it to the Mac.
 *
 * A Mac refuses to start from a volume whose Master Directory Block says it is
 * still in use: MountVol returns badMDBErr (-60) and the ROM falls through to
 * the question-mark floppy. That is a deliberate File Manager policy, and from
 * the outside it looks like an unexplained failure. Reading the MDB ourselves
 * costs two sector reads and turns it into a diagnosis.
 *
 * This only reads. Repairing a volume is a separate decision with its own
 * risks — see §Data safety in AGENTS.md: a check that rubber-stamps a damaged
 * volume would be worse than no check at all.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

extern "C" {
#include "hfs.h"
}

#define FROM "okapia-hfs"

extern size_t Sys_read (void *fh, void *buffer, loff_t offset, size_t length);

// Only two things are parsed by hand here, and both have a reason.
//
// The clean/dirty bit has to be read *before* mounting, because mounting is
// what repairs it — after that the answer is gone. And the volume has to be
// located the way Basilisk locates it (find_hfs_partition, disk.cpp:120), not
// the way libhfs would, because Basilisk is what will serve it to the Mac.
// Everything else — name, geometry, counts, dates, blessed folder — comes from
// hfs_vstat() rather than being reparsed here.
enum
{
    kMDBOffset = 1024,
    kDrAtrb    = 0x0A,          // bit 8 set = volume was unmounted cleanly
    kUnmounted = 0x0100
};

static uint16 BE16 (const uint8 *p)  { return (uint16) ((p[0] << 8) | p[1]); }
static uint32 BE32 (const uint8 *p)
{
    return ((uint32) p[0] << 24) | ((uint32) p[1] << 16)
         | ((uint32) p[2] << 8)  |  (uint32) p[3];
}

static loff_t FindVolumeStart (void *fh)
{
    uint8 Block[512];

    for (int i = 0; i < 64; i++)
    {
        if (Sys_read (fh, Block, (loff_t) i * 512, 512) != 512)
        {
            break;
        }
        if (BE16 (Block) != 0x504D)         // 'PM'
        {
            continue;
        }
        if (strcmp ((const char *) (Block + 48), "Apple_HFS") == 0)
        {
            return (loff_t) BE32 (Block + 8) * 512;
        }
    }
    return 0;
}

bool HfsInspect (void *fh, const char *pName)
{
    uint8 MDB[512];
    loff_t nStart = FindVolumeStart (fh);

    if (Sys_read (fh, MDB, nStart + kMDBOffset, 512) != 512)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "%s: cannot read the MDB", pName);
        return false;
    }
    if (BE16 (MDB) != 0x4244)               // 'BD'
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: no HFS volume at offset %lu",
                                pName, (unsigned long) (nStart + kMDBOffset));
        return false;
    }

    const uint16 nAtrb = BE16 (MDB + kDrAtrb);
    if (nAtrb & kUnmounted)
    {
        return true;
    }

    CLogger::Get ()->Write (FROM, LogWarning,
                            "%s: marked in use (drAtrb %04X) — the last session did not "
                            "shut down, and MountVol would answer badMDBErr",
                            pName, (unsigned) nAtrb);
    return false;
}

/*
 *  Repair a volume the last session left mounted.
 *
 *  libhfs does the work by itself: mounting a volume without HFS_ATRB_UMOUNTED
 *  runs v_scavenge() (volume.c:440), and unmounting sets the bit again
 *  (volume.c:140). So a mount/unmount pair is the whole repair — the same
 *  treatment the volume would get from another System, which is what makes it
 *  bootable again.
 *
 *  This writes to the user's volume, so it refuses anything it cannot mount
 *  read-write rather than guessing (AGENTS.md, Data safety).
 */

bool HfsRepair (const char *pPath)
{
    CLogger::Get ()->Write (FROM, LogNotice, "%s: repairing", pPath);

    hfsvol *pVolume = hfs_mount (pPath, 0, HFS_MODE_RDWR);
    if (pVolume == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "%s: cannot mount for repair (%s)",
                                pPath, hfs_error != 0 ? hfs_error : "no reason given");
        return false;
    }

    // Now that libhfs holds the volume, let it describe it: no point reparsing
    // fields it already understands, and it knows the blessed folder too.
    hfsvolent Ent;
    if (hfs_vstat (pVolume, &Ent) == 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "%s: \"%s\", %lu KB free of %lu KB, %lu files, "
                                "%lu folders, System folder %lu",
                                pPath, Ent.name,
                                (unsigned long) (Ent.freebytes / 1024),
                                (unsigned long) (Ent.totbytes / 1024),
                                (unsigned long) Ent.numfiles,
                                (unsigned long) Ent.numdirs,
                                (unsigned long) Ent.blessed);
    }

    // The scavenge already happened inside hfs_mount(); unmounting is what
    // records that the volume is consistent again.
    if (hfs_umount (pVolume) < 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "%s: repair did not complete (%s)",
                                pPath, hfs_error != 0 ? hfs_error : "no reason given");
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "%s: repaired and marked clean", pPath);
    return true;
}
