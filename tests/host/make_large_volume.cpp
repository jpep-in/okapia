/*
 * make_large_volume.cpp — an HFS volume whose System file lies past 2 GiB.
 *
 *   usage: make_large_volume <out.img> <size-MB> <source.img>
 *
 * The subject of scripts/check-large-volume.sh. circle-newlib's _lseek took an
 * int, so every offset from 2 GiB on was refused and a volume of 2 GB or more
 * was unreadable past that point (patches/circle-stdlib/0002). A volume only
 * proves the fix if something the kernel must read sits beyond the line, so
 * this one puts a filler file first and the System file after it, blessed, and
 * checks where libhfs actually placed it rather than trusting the order of the
 * writes. The kernel reads that System file's 'vers' resource before the
 * Macintosh starts, and says so in the log.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern "C"
{
#include "libhfs.h"
}

static const unsigned long long LINE = 1ull << 31;

static int Fail (const char *pWhat)
{
    fprintf (stderr, "make_large_volume: %s: %s\n", pWhat, hfs_error ? hfs_error : "?");
    return 1;
}

static bool CopyFork (hfsfile *pFrom, hfsfile *pTo, int nFork)
{
    hfs_setfork (pFrom, nFork);
    hfs_setfork (pTo, nFork);
    static char Buffer[64 * 1024];
    for (;;)
    {
        const long nGot = hfs_read (pFrom, Buffer, sizeof Buffer);
        if (nGot < 0)
        {
            return false;
        }
        if (nGot == 0)
        {
            return true;
        }
        if (hfs_write (pTo, Buffer, (unsigned long) nGot) != (unsigned long) nGot)
        {
            return false;
        }
    }
}

int main (int argc, char **argv)
{
    if (argc != 4)
    {
        fprintf (stderr, "usage: %s <out.img> <size-MB> <source.img>\n", argv[0]);
        return 2;
    }
    const char *pOut = argv[1];
    const unsigned long long nBytes = strtoull (argv[2], 0, 10) << 20;
    const char *pSource = argv[3];
    if (nBytes <= LINE + (64ull << 20))
    {
        fprintf (stderr, "make_large_volume: the volume must be well past 2 GiB\n");
        return 2;
    }

    // hfs_format takes the medium's size from the file, so the file comes first.
    FILE *pFile = fopen (pOut, "wb");
    if (pFile == 0 || ftruncate (fileno (pFile), (off_t) nBytes) != 0)
    {
        perror (pOut);
        return 1;
    }
    fclose (pFile);

    if (hfs_format (pOut, 0, 0, "Large", 0, 0) != 0)
    {
        return Fail ("hfs_format");
    }

    hfsvol *pSrc = hfs_mount (pSource, 0, HFS_MODE_RDONLY);
    if (pSrc == 0)
    {
        return Fail (pSource);
    }
    hfsvolent SrcEnt;
    if (hfs_vstat (pSrc, &SrcEnt) != 0 || SrcEnt.blessed == 0
        || hfs_setcwd (pSrc, SrcEnt.blessed) != 0)
    {
        return Fail ("no blessed folder on the source");
    }
    hfsfile *pSystem = hfs_open (pSrc, "System");
    if (pSystem == 0)
    {
        return Fail ("no System file in the source's blessed folder");
    }
    hfsdirent SystemEnt;
    hfs_fstat (pSystem, &SystemEnt);

    hfsvol *pVol = hfs_mount (pOut, 0, HFS_MODE_RDWR);
    if (pVol == 0)
    {
        return Fail (pOut);
    }

    // The filler: zeros, written, so its blocks are allocated and the System
    // file cannot be placed ahead of them.
    hfsfile *pFiller = hfs_create (pVol, ":Filler", "BINA", "????");
    if (pFiller == 0)
    {
        return Fail ("hfs_create Filler");
    }
    static char Zeros[1 << 20];
    for (unsigned long long nDone = 0; nDone < LINE + (32ull << 20); nDone += sizeof Zeros)
    {
        if (hfs_write (pFiller, Zeros, sizeof Zeros) != sizeof Zeros)
        {
            return Fail ("writing the filler");
        }
    }
    hfs_close (pFiller);

    if (hfs_mkdir (pVol, ":System Folder") != 0)
    {
        return Fail ("hfs_mkdir");
    }
    hfsfile *pCopy = hfs_create (pVol, ":System Folder:System",
                                 SystemEnt.u.file.type, SystemEnt.u.file.creator);
    if (pCopy == 0 || !CopyFork (pSystem, pCopy, 0) || !CopyFork (pSystem, pCopy, 1))
    {
        return Fail ("copying the System file");
    }
    hfsdirent CopyEnt;
    hfs_fstat (pCopy, &CopyEnt);
    CopyEnt.fdflags = SystemEnt.fdflags;
    hfs_fsetattr (pCopy, &CopyEnt);

    // Where the resource fork — the one the kernel reads — actually begins.
    const unsigned long long nAt =
          (unsigned long long) pVol->mdb.drAlBlSt * 512
        + (unsigned long long) pCopy->cat.u.fil.filRExtRec[0].xdrStABN * pVol->mdb.drAlBlkSiz;
    hfs_close (pCopy);
    hfs_close (pSystem);

    hfsdirent Folder;
    hfsvolent Ent;
    if (   hfs_stat (pVol, ":System Folder", &Folder) != 0
        || hfs_vstat (pVol, &Ent) != 0)
    {
        return Fail ("hfs_stat");
    }
    Ent.blessed = Folder.cnid;
    if (hfs_vsetattr (pVol, &Ent) != 0)
    {
        return Fail ("hfs_vsetattr");
    }

    hfs_umount (pVol);
    hfs_umount (pSrc);

    printf ("%s: %llu MB, System resource fork at %llu MB\n",
            pOut, nBytes >> 20, nAt >> 20);
    if (nAt < LINE)
    {
        fprintf (stderr, "make_large_volume: the System file landed below 2 GiB\n");
        return 1;
    }
    return 0;
}
