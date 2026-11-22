//
// cards_desktop.cpp — the SD cards a desktop can see, for the welcome page.
//
// What the installer's card list did (okapia-installer core/src/card.rs, list
// and describe): the removable volumes mounted now, and for any folder its
// volume's file system, size, free space and cluster. Read only.
//
// macOS: getmntinfo for the mounts, DiskArbitration to keep only what is
// ejectable or removable and not a disk image (a mounted .dmg is ejectable
// too). On msdosfs the fundamental block size is the cluster.
// Linux: /proc/mounts, and the block device's own "removable" in /sys (an SD
// reader's mmcblk counts as one). The cluster is statfs's f_bsize there too.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_desktop.h"
#include "okapia_host.h"
#include "install.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "okapia_card.h"
#include <sys/param.h>
#include <sys/mount.h>

#ifdef __APPLE__
// DiskArbitration, in menus_desktop.mm: CoreFoundation's <Block.h> meets
// libhfs's block.h on a case-insensitive disk, so the call lives in the file
// built without the project's include paths.
bool DesktopDeviceRemovable (const char *pDevice);
#else
#include <mntent.h>
#include <sys/statfs.h>
#endif

void HostTidy (const char *pCardPath)
{
    // "._okapia.rom" beside "/okapia.rom": macOS's provenance attribute, kept
    // in a file of its own on FAT. Removed only when it is one (install_card.cpp).
    DropAppleDouble (CardPath (pCardPath));
}

static const char *BaseName (const char *pPath)
{
    const char *pSlash = strrchr (pPath, '/');
    return pSlash != 0 && pSlash[1] != '\0' ? pSlash + 1 : pPath;
}

static void Fill (TCardVolume *pVolume, const char *pMount, const char *pFs, unsigned long long nBlocks,
                  unsigned long long nAvail, unsigned long long nBlockSize)
{
    memset (pVolume, 0, sizeof *pVolume);
    snprintf (pVolume->Mount, sizeof pVolume->Mount, "%s", pMount);
    snprintf (pVolume->Name, sizeof pVolume->Name, "%s", BaseName (pMount));
    snprintf (pVolume->FsName, sizeof pVolume->FsName, "%s", pFs);
    pVolume->nTotal = nBlocks * nBlockSize;
    pVolume->nFree  = nAvail * nBlockSize;
    const bool bFat = strcmp (pFs, "msdos") == 0 || strcmp (pFs, "vfat") == 0
                   || strcmp (pFs, "fat") == 0 || strcmp (pFs, "fat32") == 0;
    if (bFat)
    {
        pVolume->Fs = CardFatKind (nBlocks);
        pVolume->nCluster = (unsigned) nBlockSize;
    }
    else
    {
        pVolume->Fs = pFs[0] != '\0' ? CardFsOther : CardFsUnknown;
    }
}

#ifdef __APPLE__

unsigned HostCards (TCardVolume *pList, unsigned nMax)
{
    struct statfs *pMounts = 0;
    const int nMounts = getmntinfo (&pMounts, MNT_NOWAIT);
    unsigned n = 0;
    for (int i = 0; i < nMounts && n < nMax; i++)
    {
        const struct statfs &M = pMounts[i];
        if ((M.f_flags & MNT_LOCAL) == 0 || strncmp (M.f_mntonname, "/Volumes/", 9) != 0
            || !DesktopDeviceRemovable (M.f_mntfromname))
        {
            continue;
        }
        Fill (&pList[n], M.f_mntonname, M.f_fstypename, M.f_blocks, M.f_bavail, M.f_bsize);
        pList[n].bRemovable = true;
        n++;
    }
    return n;
}

bool HostVolumeOf (const char *pHostPath, TCardVolume *pVolume)
{
    struct statfs S;
    if (statfs (pHostPath, &S) != 0)
    {
        return false;
    }
    Fill (pVolume, S.f_mntonname, S.f_fstypename, S.f_blocks, S.f_bavail, S.f_bsize);
    TCardVolume Cards[16];
    const unsigned nCards = HostCards (Cards, 16);
    for (unsigned i = 0; i < nCards; i++)
    {
        if (strcmp (Cards[i].Mount, S.f_mntonname) == 0)
        {
            pVolume->bRemovable = true;
        }
    }
    return true;
}

#else

static bool RemovableDevice (const char *pDevice)
{
    // /dev/sdb1 → sdb, /dev/mmcblk0p1 → mmcblk0
    const char *pName = strncmp (pDevice, "/dev/", 5) == 0 ? pDevice + 5 : 0;
    if (pName == 0)
    {
        return false;
    }
    char Base[64];
    snprintf (Base, sizeof Base, "%s", pName);
    if (strncmp (Base, "mmcblk", 6) == 0)
    {
        char *p = strchr (Base, 'p');
        if (p != 0)
            *p = '\0';
        return true;                    // an SD reader
    }
    size_t n = strlen (Base);
    while (n > 0 && Base[n - 1] >= '0' && Base[n - 1] <= '9')
        Base[--n] = '\0';
    char Path[128];
    snprintf (Path, sizeof Path, "/sys/block/%s/removable", Base);
    FILE *pFile = fopen (Path, "r");
    if (pFile == 0)
    {
        return false;
    }
    const int c = fgetc (pFile);
    fclose (pFile);
    return c == '1';
}

static const char *FsOf (const struct statfs &S, const char *pType)
{
    return S.f_type == 0x4d44 ? "vfat" : pType;
}

unsigned HostCards (TCardVolume *pList, unsigned nMax)
{
    FILE *pMounts = setmntent ("/proc/mounts", "r");
    if (pMounts == 0)
    {
        return 0;
    }
    unsigned n = 0;
    struct mntent *pEntry;
    while (n < nMax && (pEntry = getmntent (pMounts)) != 0)
    {
        if (!RemovableDevice (pEntry->mnt_fsname))
        {
            continue;
        }
        struct statfs S;
        if (statfs (pEntry->mnt_dir, &S) != 0)
        {
            continue;
        }
        Fill (&pList[n], pEntry->mnt_dir, FsOf (S, pEntry->mnt_type), S.f_blocks, S.f_bavail, S.f_bsize);
        pList[n].bRemovable = true;
        n++;
    }
    endmntent (pMounts);
    return n;
}

bool HostVolumeOf (const char *pHostPath, TCardVolume *pVolume)
{
    struct statfs S;
    if (statfs (pHostPath, &S) != 0)
    {
        return false;
    }
    Fill (pVolume, pHostPath, FsOf (S, ""), S.f_blocks, S.f_bavail, S.f_bsize);
    TCardVolume Cards[16];
    const unsigned nCards = HostCards (Cards, 16);
    for (unsigned i = 0; i < nCards; i++)
    {
        const size_t nMount = strlen (Cards[i].Mount);
        if (strncmp (pHostPath, Cards[i].Mount, nMount) == 0
            && (pHostPath[nMount] == '\0' || pHostPath[nMount] == '/'))
        {
            *pVolume = Cards[i];
        }
    }
    return true;
}

#endif
