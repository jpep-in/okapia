/*
 * posix_bits.cpp — the last few libc functions newlib does not carry.
 *
 * Each of these is reached from a code path Okapia does not take, but the calls
 * are chosen at run time rather than compiled out, so the symbols must exist.
 * They report instead of pretending to succeed — with the exception of sleep,
 * which has a real meaning here.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "okapia_circle.h"

extern "C" {

/* Used by prefs and extfs to test for a file. FatFs answers through stat(). */
int access (const char *path, int mode)
{
    struct stat st;
    if (stat (path, &st) != 0)
    {
        errno = ENOENT;
        return -1;
    }
    return 0;                   /* exists; FAT has no permissions to check */
}

/* creat() is open() with a fixed flag set. */
int creat (const char *path, mode_t mode)
{
    return open (path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

/* Real behaviour: Circle can wait, and callers mean it. */
unsigned sleep (unsigned seconds)
{
    CTimer::SimpleMsDelay (seconds * 1000);
    return 0;
}

/* Only ether.cpp's UDP tunnel wants this, and Okapia never takes that path. */
// SheepShaver's vm_alloc.cpp asks the host how big a page is. A real answer,
// not a report: it uses it to round allocations, and Circle's tables are built
// on 4 KB pages like everything else on AArch64 here.
int getpagesize (void)
{
    return 4096;
}

int gethostname (char *name, size_t len)
{
    static const char Name[] = "okapia";
    if (name == 0 || len < sizeof Name)
    {
        errno = EINVAL;
        return -1;
    }
    __builtin_memcpy (name, Name, sizeof Name);
    return 0;
}

}
