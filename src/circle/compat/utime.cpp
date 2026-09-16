/*
 * utime.cpp — file timestamps, which newlib declares but does not implement.
 *
 * extfs_unix.cpp calls utime() to give a file in the shared folder the dates
 * the Mac asked for. FatFs does exactly this through f_utime, which needs
 * FF_USE_CHMOD — on in circle's ffconf.h — so there is nothing to emulate.
 *
 * A file dropped on the shared folder from the Mac keeps its date because of
 * this; without it the Finder shows every file as created in 1980, which is
 * what an all-zero FAT timestamp means.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "utime.h"
#include <errno.h>
#include <time.h>

#include <wrap_fatfs.h>

extern "C" int utime (const char *path, const struct utimbuf *times)
{
    if (path == 0)
    {
        errno = EFAULT;
        return -1;
    }

    // No times given means "now", which is what FatFs writes by itself.
    time_t nWhen = times != 0 ? times->modtime : time (0);

    struct tm When;
    if (gmtime_r (&nWhen, &When) == 0 || When.tm_year < 80)
    {
        // FAT dates start in 1980 and cannot express anything earlier. Refusing
        // is better than folding the date into 1980 and calling it preserved.
        errno = EINVAL;
        return -1;
    }

    FILINFO Info;
    Info.fdate = (WORD) (((When.tm_year - 80) << 9)
                       | ((When.tm_mon + 1) << 5)
                       |   When.tm_mday);
    Info.ftime = (WORD) ((When.tm_hour << 11)
                       | (When.tm_min << 5)
                       | (When.tm_sec / 2));

    if (f_utime (path, &Info) != FR_OK)
    {
        errno = EIO;
        return -1;
    }
    return 0;
}
