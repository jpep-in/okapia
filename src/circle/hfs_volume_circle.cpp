/*
 * hfs_volume_circle.cpp — look at the volume before handing it to the Mac.
 *
 * A Mac refuses to start from a volume whose Master Directory Block says it is
 * still in use: MountVol returns badMDBErr (-60) and the ROM falls through to
 * the question-mark floppy. That is a deliberate File Manager policy, and from
 * the outside it looks like an unexplained failure. Reading the MDB ourselves
 * costs two sector reads and turns it into a diagnosis.
 *
 * The same two mount modes cover the rest of this file. Read-only, libhfs
 * writes nothing at all — not even the scavenge (volume.c:1059) — so it can
 * describe every image on the card without touching one. Read-write, mounting
 * *is* the repair. Which of the two runs is a preference, never a default this
 * file decides on its own: see docs/topics/storage.md.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "hfs_volume_circle.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "hfs.h"
}

#define FROM "okapia-hfs"

extern size_t Sys_read (void *fh, void *buffer, loff_t offset, size_t length);
extern void *Sys_open (const char *name, bool read_only, bool is_cdrom);
extern void Sys_close (void *fh);

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

// Read drAtrb without mounting. Returns false both for "dirty" and for "not an
// HFS volume at all"; the caller finds out which from the log, and either way
// the answer to "may the Mac boot this as it stands" is no.
//
// bQuiet is for the inventory, which asks about every file whose name looks
// like an image — on a real card that includes kernel8.img — and must not
// report a miss. A warning that fires on every boot is the one nobody reads,
// and this is the same warning that reports a genuinely unreadable volume.
static bool InspectHandle (void *fh, const char *pName, bool bQuiet)
{
    uint8 MDB[512];
    loff_t nStart = FindVolumeStart (fh);

    if (Sys_read (fh, MDB, nStart + kMDBOffset, 512) != 512)
    {
        if (!bQuiet)
        {
            CLogger::Get ()->Write (FROM, LogWarning, "%s: cannot read the MDB", pName);
        }
        return false;
    }
    if (BE16 (MDB) != 0x4244)               // 'BD'
    {
        if (!bQuiet)
        {
            CLogger::Get ()->Write (FROM, LogWarning,
                                    "%s: no HFS volume at offset %lu",
                                    pName, (unsigned long) (nStart + kMDBOffset));
        }
        return false;
    }

    const uint16 nAtrb = BE16 (MDB + kDrAtrb);
    if (nAtrb & kUnmounted)
    {
        return true;
    }

    if (!bQuiet)
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: marked in use (drAtrb %04X) — the last session did not "
                                "shut down, and MountVol would answer badMDBErr",
                                pName, (unsigned) nAtrb);
    }
    return false;
}

static bool InspectPath (const char *pPath, bool bQuiet)
{
    void *fh = Sys_open (pPath, true, false);
    if (fh == 0)
    {
        if (!bQuiet)
        {
            CLogger::Get ()->Write (FROM, LogWarning, "%s: cannot open", pPath);
        }
        return false;
    }

    bool bClean = InspectHandle (fh, pPath, bQuiet);
    Sys_close (fh);
    return bClean;
}

bool HfsInspect (const char *pPath)
{
    return InspectPath (pPath, false);
}

/*
 *  Describe a volume without touching it.
 *
 *  hfs_vstat() answers everything the firmware needs to offer a choice of
 *  System, and one field answers it exactly rather than by guesswork: blessed
 *  is the CNID the Mac itself looks up to find the System folder, so a non-zero
 *  value means the ROM will find something to start. A file name never tells
 *  you that.
 */

// Mount whatever kind of image this is, without writing to it.
//
// libhfs takes a partition number, and the two kinds of image on a card answer
// to different ones: a flat disk image is partition 0 — the whole file is the
// volume — while a CD carries an Apple partition map and its HFS volume is
// partition 1. Asking for the wrong one fails outright ("not a Macintosh HFS
// volume" one way, "invalid partition map" the other), so both are tried.
//
// Measured on a Mac OS 8.6 install CD: partition 0 refused it, partition 1
// mounted "Mac OS 8.6", 600 MB, blessed folder and all. Its driver descriptor
// announces 2048-byte blocks while its partition map is written at 512, which
// is why the emulator's own find_hfs_partition() — 512 throughout,
// cdrom.cpp:194 — reads it correctly and a stricter reader does not.
static hfsvol *MountReadOnly (const char *pPath)
{
    hfsvol *pVolume = hfs_mount (pPath, 0, HFS_MODE_RDONLY);
    if (pVolume == 0)
    {
        pVolume = hfs_mount (pPath, 1, HFS_MODE_RDONLY);
    }
    return pVolume;
}

bool HfsDescribe (const char *pPath, THfsVolumeInfo *pInfo)
{
    memset (pInfo, 0, sizeof *pInfo);
    snprintf (pInfo->Path, sizeof pInfo->Path, "%s", pPath);

    // Whether libhfs can mount it is what makes a file a volume, so ask that
    // first: everything else is only meaningful once it answers yes, and the
    // inventory calls this on every file whose name looks like an image.
    hfsvol *pVolume = MountReadOnly (pPath);
    if (pVolume == 0)
    {
        return false;
    }

    // The clean bit is the one field libhfs does not report, so it is read by
    // hand. Order does not matter: a read-only mount writes nothing at all,
    // the scavenge included (volume.c:1059), so the bit on the card is
    // untouched either way.
    pInfo->bClean = InspectPath (pPath, true);

    hfsvolent Ent;
    bool bOk = hfs_vstat (pVolume, &Ent) == 0;
    if (bOk)
    {
        snprintf (pInfo->Name, sizeof pInfo->Name, "%s", Ent.name);
        pInfo->TotalKB  = (unsigned long) (Ent.totbytes / 1024);
        pInfo->FreeKB   = (unsigned long) (Ent.freebytes / 1024);
        pInfo->NumFiles = (unsigned long) Ent.numfiles;
        pInfo->NumDirs  = (unsigned long) Ent.numdirs;
        pInfo->Blessed  = (unsigned long) Ent.blessed;
        pInfo->nLastModified = (long) Ent.mddate;
    }

    hfs_umount (pVolume);
    return bOk;
}

/*
 *  Every HFS volume in the root of the card.
 *
 *  Extensions are only a filter to keep the ROM and the preferences file out of
 *  the way; what makes an entry a volume is that libhfs mounts it. So a disk
 *  image under any of the names the Mac world uses is found, and nothing else
 *  is reported as one.
 */

static bool LooksLikeAnImage (const char *pName)
{
    // .toast and .iso are CD images and carry a partition map rather than being
    // a bare volume; MountReadOnly() takes both kinds, so what is behind the
    // name no longer decides what may be listed.
    static const char *Suffixes[] = { ".image", ".img", ".hda", ".dsk", ".hfv",
                                      ".iso", ".toast", 0 };

    const char *pDot = strrchr (pName, '.');
    if (pDot == 0)
    {
        return false;
    }
    for (unsigned i = 0; Suffixes[i] != 0; i++)
    {
        if (strcasecmp (pDot, Suffixes[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

unsigned HfsInventory (THfsVolumeInfo *pList, unsigned nMax)
{
    DIR *pDir = opendir ("/");
    if (pDir == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Cannot read the card's root directory");
        return 0;
    }

    unsigned nFound = 0;
    struct dirent *pEntry;
    while (nFound < nMax && (pEntry = readdir (pDir)) != 0)
    {
        if (!LooksLikeAnImage (pEntry->d_name))
        {
            continue;
        }

        // Built by hand rather than with snprintf: a truncated path would
        // name a different file, so a name that does not fit is skipped.
        char Path[64];
        size_t nLen = strlen (pEntry->d_name);
        if (nLen + 2 > sizeof Path)
        {
            CLogger::Get ()->Write (FROM, LogWarning, "Name too long, skipped: %s",
                                    pEntry->d_name);
            continue;
        }
        Path[0] = '/';
        memcpy (Path + 1, pEntry->d_name, nLen + 1);

        if (HfsDescribe (Path, &pList[nFound]))
        {
            nFound++;
        }
    }

    closedir (pDir);
    return nFound;
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
 *  read-write rather than guessing (docs/topics/storage.md).
 */

bool HfsFormat (const char *pPath, const char *pName)
{
    CLogger::Get ()->Write (FROM, LogNotice, "%s: formatting as \"%s\"", pPath, pName);

    // Mode 0: no options. HFS_OPT_2048 is for media whose blocks are 2048 bytes,
    // which a file on a FAT card is not.
    if (hfs_format (pPath, 0, 0, pName, 0, 0) < 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "%s: cannot format (%s)", pPath,
                                hfs_error != 0 ? hfs_error : "no reason given");
        return false;
    }

    // Mounted straight back, read-only, and asked what it is. A format that
    // wrote something the next mount cannot read is a volume that will be found
    // broken later, by somebody who has since put files on it — so it is found
    // here instead, while the only thing at stake is an empty file.
    hfsvol *pVolume = hfs_mount (pPath, 0, HFS_MODE_RDONLY);
    if (pVolume == 0)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "%s: formatted but will not mount (%s)", pPath,
                                hfs_error != 0 ? hfs_error : "no reason given");
        return false;
    }
    hfsvolent Ent;
    if (hfs_vstat (pVolume, &Ent) == 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "%s: \"%s\", %lu KB free of %lu KB",
                                pPath, Ent.name,
                                (unsigned long) (Ent.freebytes / 1024),
                                (unsigned long) (Ent.totbytes / 1024));
    }
    hfs_umount (pVolume);
    return true;
}

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

/*
 *  Which System is installed on a volume
 *
 *  The Mac's model id has to match the System, not the ROM: a System 7.1 with
 *  its enabler checks the machine it is running on and stops on an empty
 *  Welcome box if it is told it is a Quadra 900. So the version has to be read
 *  before the emulator starts, from the volume itself.
 *
 *  Two things make this exact rather than a guess. The System file is found by
 *  type and creator ('zsys' / 'MACS'), not by name — the name is localised, and
 *  a French System is called "Système". And the version comes from its 'vers'
 *  resource, which is where the Finder's Get Info reads it too.
 *
 *  Read-only throughout: libhfs writes nothing on a volume mounted that way,
 *  hfs_setfork's truncation included (file.c:16).
 */

// Classic resource fork layout. Offsets are from the start of the fork unless
// said otherwise; every multi-byte field is big-endian.
enum
{
    kResHeaderLen   = 16,
    kResMapTypeOff  = 24,       // in the map: offset to the type list
    kRefEntryLen    = 12,
    kTypeEntryLen   = 8
};

static bool ReadAt (hfsfile *pFile, unsigned long nOffset, void *pBuffer, unsigned long nLength)
{
    if (hfs_seek (pFile, (long) nOffset, HFS_SEEK_SET) == (unsigned long) -1)
    {
        return false;
    }
    return hfs_read (pFile, pBuffer, nLength) == nLength;
}

// Locate one resource type in the map: where its reference list is, how many
// references it holds, and where the data area starts. False when the fork is
// not a resource fork, or when the type is simply not in it.
//
// Split out of FindResource because presence is a question of its own: the
// System's flavour is read from two types whose data nobody wants.
static bool FindTypeList (hfsfile *pFile, const char *pType,
                          uint32 *pnDataOff, uint32 *pnRefList, unsigned *pnRefs)
{
    uint8 Header[kResHeaderLen];
    if (!ReadAt (pFile, 0, Header, sizeof Header))
    {
        return false;
    }

    *pnDataOff = BE32 (Header);
    const uint32 nMapOff = BE32 (Header + 4);

    uint8 MapHead[4];
    if (!ReadAt (pFile, nMapOff + kResMapTypeOff, MapHead, sizeof MapHead))
    {
        return false;
    }
    const uint32 nTypeList = nMapOff + BE16 (MapHead);

    uint8 Count[2];
    if (!ReadAt (pFile, nTypeList, Count, sizeof Count))
    {
        return false;
    }
    const unsigned nTypes = (unsigned) BE16 (Count) + 1;

    for (unsigned i = 0; i < nTypes; i++)
    {
        uint8 Entry[kTypeEntryLen];
        if (!ReadAt (pFile, nTypeList + 2 + i * kTypeEntryLen, Entry, sizeof Entry))
        {
            return false;
        }
        if (memcmp (Entry, pType, 4) != 0)
        {
            continue;
        }
        *pnRefs    = (unsigned) BE16 (Entry + 4) + 1;
        *pnRefList = nTypeList + BE16 (Entry + 6);
        return true;
    }
    return false;
}

// Is there at least one resource of this type? Not the same question as
// FindResource returning non-zero: that answers with a length, and a resource
// of length zero exists just as much as any other.
static bool HasResourceType (hfsfile *pFile, const char *pType)
{
    uint32   nDataOff, nRefList;
    unsigned nRefs;
    return FindTypeList (pFile, pType, &nDataOff, &nRefList, &nRefs);
}

// Find one resource of the given type, preferring the lowest id, and leave the
// fork positioned on its data. Returns its length, or 0.
static unsigned long FindResource (hfsfile *pFile, const char *pType)
{
    uint32   nDataOff, nRefList;
    unsigned nRefs;
    if (!FindTypeList (pFile, pType, &nDataOff, &nRefList, &nRefs))
    {
        return 0;
    }

    // The lowest id is the resource that describes the file itself; higher ones
    // describe the package it belongs to, which is not what we are after.
    uint32 nBest = 0;
    bool bFound  = false;
    unsigned nBestId = 0;
    for (unsigned i = 0; i < nRefs; i++)
    {
        uint8 Ref[kRefEntryLen];
        if (!ReadAt (pFile, nRefList + i * kRefEntryLen, Ref, sizeof Ref))
        {
            return 0;
        }
        const unsigned nId = (unsigned) BE16 (Ref);
        if (bFound && nId >= nBestId)
        {
            continue;
        }
        bFound   = true;
        nBestId  = nId;
        nBest    = ((uint32) Ref[5] << 16) | ((uint32) Ref[6] << 8) | (uint32) Ref[7];
    }
    if (!bFound)
    {
        return 0;
    }

    uint8 Length[4];
    if (!ReadAt (pFile, nDataOff + nBest, Length, sizeof Length))
    {
        return 0;
    }
    return BE32 (Length);       // the fork is now positioned on the data itself
}

// Open the System file of the blessed folder, resource fork selected, and
// report its name. The name is localised — "Système" on the volumes staged here
// — so the file is found by type and creator and never by name. Null when the
// volume has no blessed folder, no System file in it, or no resource fork.
static hfsfile *OpenSystemFile (hfsvol *pVolume, char *pName, size_t nNameSize)
{
    hfsvolent Ent;
    if (hfs_vstat (pVolume, &Ent) < 0 || Ent.blessed == 0)
    {
        return 0;
    }
    if (hfs_setcwd (pVolume, Ent.blessed) < 0)
    {
        return 0;
    }

    // ":" is libhfs for "the current directory" (volume.c, v_resolve).
    hfsdir *pDir = hfs_opendir (pVolume, ":");
    if (pDir == 0)
    {
        return 0;
    }

    hfsfile *pFile = 0;
    hfsdirent DirEnt;
    while (hfs_readdir (pDir, &DirEnt) == 0)
    {
        if (DirEnt.flags & HFS_ISDIR)
        {
            continue;
        }
        if (strcmp (DirEnt.u.file.type, "zsys") != 0
            || strcmp (DirEnt.u.file.creator, "MACS") != 0)
        {
            continue;
        }

        if (pName != 0)
        {
            snprintf (pName, nNameSize, "%s", DirEnt.name);
        }

        pFile = hfs_open (pVolume, DirEnt.name);
        if (pFile != 0 && hfs_setfork (pFile, 1) < 0)       // 1 = resource fork
        {
            hfs_close (pFile);
            pFile = 0;
        }
        break;
    }

    hfs_closedir (pDir);
    return pFile;
}

bool HfsSystemVersion (const char *pPath, THfsSystemVersion *pVersion)
{
    memset (pVersion, 0, sizeof *pVersion);

    hfsvol *pVolume = MountReadOnly (pPath);
    if (pVolume == 0)
    {
        return false;
    }

    bool bOk = false;
    hfsfile *pFile = OpenSystemFile (pVolume, pVersion->File, sizeof pVersion->File);
    if (pFile != 0)
    {
        // Asked before 'vers', because FindResource leaves the fork positioned
        // on the data it found and the read below depends on that.
        pVersion->bNativeCode = HasResourceType (pFile, "cfrg");

        {
            unsigned long nLength = FindResource (pFile, "vers");
            uint8 Vers[24];
            memset (Vers, 0, sizeof Vers);

            // Read no more than the resource holds, and trust only what came
            // back: hfs_read is free to return less, and the bytes past that
            // are neither the resource's nor initialised.
            unsigned long nWanted = nLength < sizeof Vers ? nLength : sizeof Vers;
            unsigned long nGot = nWanted > 0 ? hfs_read (pFile, Vers, nWanted) : 0;
            if (nGot >= 7)
            {
                // 'vers': BCD major, then minor and bugfix as one BCD byte,
                // stage, prerelease, region, then a Pascal short version string.
                pVersion->nMajor  = ((Vers[0] >> 4) & 0x0F) * 10 + (Vers[0] & 0x0F);
                pVersion->nMinor  = (Vers[1] >> 4) & 0x0F;
                pVersion->nBugfix = Vers[1] & 0x0F;

                // The Pascal string's own length byte is data from the volume,
                // so it is clamped to what was actually read and to the
                // destination — in that order, because a short read is the
                // case where trusting the length byte reads someone else's
                // bytes, or the stack.
                unsigned nShort = Vers[6];
                if (nShort > nGot - 7)
                {
                    nShort = (unsigned) (nGot - 7);
                }
                if (nShort > sizeof pVersion->Short - 1)
                {
                    nShort = sizeof pVersion->Short - 1;
                }
                memcpy (pVersion->Short, Vers + 7, nShort);
                pVersion->Short[nShort] = '\0';

                bOk = true;
            }
        }
        hfs_close (pFile);
    }

    hfs_umount (pVolume);
    return bOk;
}

THfsSystemFlavour HfsFlavourOf (const THfsSystemVersion *pVersion)
{
    if (pVersion->nMajor == 0)
    {
        return HfsFlavourUnreadable;
    }

    // 7.6.1 becomes 761, 8.5 becomes 850. The two halves of the second BCD byte
    // are each one digit, so no version can collide with its neighbour.
    const unsigned nCode = pVersion->nMajor * 100 + pVersion->nMinor * 10
                         + pVersion->nBugfix;

    if (nCode < 752)
    {
        return HfsFlavour68k;
    }
    if (nCode >= 850)
    {
        return HfsFlavourPowerPC;
    }
    return pVersion->bNativeCode ? HfsFlavourUniversal : HfsFlavour68k;
}

bool HfsDateIsPlausible (long nWhen, long nBuildLocal)
{
    // >=, not >: the saturated value is representable, and that is precisely
    // why a first guard written as "beyond what HFS can hold" let it through.
    if (nWhen >= HFS_SATURATED_DATE)
    {
        return false;
    }
    return nWhen <= nBuildLocal + HFS_PLAUSIBLE_AHEAD;
}
