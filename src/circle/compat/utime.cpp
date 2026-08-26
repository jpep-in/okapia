/*
 * utime.cpp — file timestamps, which newlib declares but does not implement.
 *
 * extfs_unix.cpp calls utime() to give a shared file the modification date the
 * Mac asked for. FatFs can do this through f_utime, but nothing in the boot path
 * depends on it, so this reports and moves on. Wiring it to f_utime is a small,
 * self-contained improvement for the day file dates start to matter.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "utime.h"
#include <errno.h>
#include <stdio.h>

extern "C" int utime (const char *path, const struct utimbuf *times)
{
    static bool bReported = false;
    if (!bReported)
    {
        // Once: this can be called for every file copied to the shared folder.
        fprintf (stderr, "okapia: utime() is a stub, file dates are not preserved\n");
        bReported = true;
    }
    (void) path;
    (void) times;
    return 0;                   // claim success: the Mac cannot act on failure
}
