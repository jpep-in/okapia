/*
 * okapia_circle.h — the desktop's stand-in for the Circle bridge.
 *
 * The platform files shared with the Pi (prefs_circle.cpp, hfs_volume_circle.cpp,
 * xpram_circle.cpp…) include src/circle/okapia_circle.h for Circle's logger and
 * types. This header is force-included ahead of it, and its include guard then
 * makes the real one a no-op: the log goes to HostLog(), the types to <stdint.h>.
 * The same trick as tests/host/shims/, with a log that speaks.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CIRCLE_H
#define OKAPIA_CIRCLE_H

// config.h declares htons and friends as plain functions, and a host libc that
// defines them as macros turns those declarations into a parse error.
#include <arpa/inet.h>
#undef htons
#undef ntohs
#undef htonl
#undef ntohl

#include <stdarg.h>
#include <stdio.h>
#include <circle/types.h>
#include <circle/memory.h>
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <circle/synchronize.h>

#ifndef ASSERT_STATIC
#define ASSERT_STATIC(expr) static_assert (expr, #expr)
#endif

enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };

// Defined by the desktop host (okapia_host_desktop.cpp).
void OkapiaHostLogV (const char *pFrom, int nSeverity, const char *pFormat, va_list Args);

class CLogger
{
public:
    static CLogger *Get (void) { static CLogger Instance; return &Instance; }
    void Write (const char *pFrom, TLogSeverity Severity, const char *pFormat, ...)
        __attribute__ ((format (printf, 4, 5)))
    {
        va_list Args;
        va_start (Args, pFormat);
        OkapiaHostLogV (pFrom, (int) Severity, pFormat, Args);
        va_end (Args);
    }
};

bool PerfReportWanted (void);
bool SerialLogWanted (void);

#include "hal_circle.h"

#endif
