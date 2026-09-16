// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * debug.h — diagnostic override, not part of the build.
 *
 * Basilisk files each do "#define DEBUG 0" before including this header, so the
 * usual -DDEBUG=1 cannot reach them. This copy is placed earlier on the include
 * path for selected files only, turning their D(bug(...)) traces back on.
 * bug is printf, and stdio goes to the serial log.
 */
#ifndef OKAPIA_TRACE_DEBUG_H
#define OKAPIA_TRACE_DEBUG_H

#include <stdio.h>
#define bug printf

#undef D
#define D(x) (x);

#endif
