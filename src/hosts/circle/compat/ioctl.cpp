/*
 * ioctl.cpp — see compat/sys/ioctl.h. A stub that says so.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <sys/ioctl.h>
#include <errno.h>
#include <stdio.h>

/* Reports through stdio rather than CLogger: compat files are built with the
   core's flags, and pulling Circle headers in here drags in macros the core's
   translation units do not set up. */
int ioctl (int fd, unsigned long request, ...)
{
    fprintf (stderr, "okapia: ioctl(%d, 0x%lx) is not implemented\n", fd, request);
    errno = ENOSYS;
    return -1;
}
