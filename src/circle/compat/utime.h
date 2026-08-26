/*
 * utime.h — newlib ships <utime.h> but never declares utime(), and its
 * <sys/utime.h> uses time_t without including <time.h>. Both are fixed here.
 * This file precedes newlib's on the include path.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_UTIME_H
#define OKAPIA_UTIME_H

#include <time.h>          /* sys/utime.h needs time_t and does not include it */
#include <sys/utime.h>     /* struct utimbuf */

#ifdef __cplusplus
extern "C" {
#endif

int utime (const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
