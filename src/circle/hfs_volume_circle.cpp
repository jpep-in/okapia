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

#define FROM "okapia-hfs"

extern size_t Sys_read (void *fh, void *buffer, loff_t offset, size_t length);

// Master Directory Block, at offset 1024 of the volume (Inside Macintosh IV).
enum
{
    kMDBOffset      = 1024,
    drSigWord       = 0x00,     // 'BD'
    drAtrb          = 0x0A,     // bit 8 set = volume was unmounted cleanly
    drNmFls         = 0x0C,
    drAlBlkSiz      = 0x14,
    drNmAlBlks      = 0x12,
    drFreeBks       = 0x22,
    drVN            = 0x24      // Str27 volume name
};

static uint16 BE16 (const uint8 *p)  { return (uint16) ((p[0] << 8) | p[1]); }
static uint32 BE32 (const uint8 *p)
{
    return ((uint32) p[0] << 24) | ((uint32) p[1] << 16)
         | ((uint32) p[2] << 8)  |  (uint32) p[3];
}

/*
 *  Where the HFS volume starts inside the file: 0 for a bare image, or the
 *  Apple_HFS partition for a real disk image. Same scan Basilisk does
 *  (find_hfs_partition, disk.cpp:120), so we look at what it will look at.
 */

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

/*
 *  Reports what the Mac is about to find. Returns true when the volume claims
 *  to have been unmounted cleanly, which is the state it will accept.
 */

bool HfsInspect (void *fh, const char *pName)
{
    uint8 MDB[512];
    loff_t nStart = FindVolumeStart (fh);

    if (Sys_read (fh, MDB, nStart + kMDBOffset, 512) != 512)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "%s: cannot read the MDB", pName);
        return false;
    }

    if (BE16 (MDB + drSigWord) != 0x4244)   // 'BD'
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: no HFS volume at offset %lu",
                                pName, (unsigned long) (nStart + kMDBOffset));
        return false;
    }

    // The volume name is a Pascal string; keep it short and safe.
    char Name[28];
    unsigned nLen = MDB[drVN];
    if (nLen > sizeof Name - 1)
    {
        nLen = sizeof Name - 1;
    }
    memcpy (Name, MDB + drVN + 1, nLen);
    Name[nLen] = '\0';

    const uint16 nAtrb  = BE16 (MDB + drAtrb);
    const bool   bClean = (nAtrb & 0x0100) != 0;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "%s: \"%s\", %u blocks of %u bytes, %u free, drAtrb %04X",
                            pName, Name,
                            (unsigned) BE16 (MDB + drNmAlBlks),
                            (unsigned) BE32 (MDB + drAlBlkSiz),
                            (unsigned) BE16 (MDB + drFreeBks),
                            (unsigned) nAtrb);

    if (!bClean)
    {
        // Say what will happen, rather than letting it look like a mystery.
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: marked in use — the last session did not shut down. "
                                "MountVol will answer badMDBErr and the Mac will not start "
                                "from it. Mount it from another System to have it repaired.",
                                pName);
    }
    return bClean;
}
