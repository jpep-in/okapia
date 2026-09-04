/*
 * okapia_circle.h — the host's stand-in for the Circle bridge.
 *
 * Two of the platform files are worth testing here rather than on a card:
 * prefs_circle.cpp, where a mistake silently loses somebody's settings, and
 * hfs_volume_circle.cpp, which reads volumes we must never damage. Neither
 * touches Circle for anything but the log, so this header is the whole cost of
 * building them with the host compiler — it is force-included ahead of the real
 * one, whose include guard then makes it a no-op.
 *
 * The undefs come first for a reason: config.h declares htons and friends as
 * plain functions, and a host libc that defines them as macros turns those
 * declarations into a parse error.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CIRCLE_H
#define OKAPIA_CIRCLE_H

#include <arpa/inet.h>
#undef htons
#undef ntohs
#undef htonl
#undef ntohl

#include <stdio.h>

enum TLogSeverity { LogPanic, LogError, LogWarning, LogNotice, LogDebug };

// Silent on purpose: these two files log freely, and a test that prints their
// running commentary is a test nobody reads.
class CLogger
{
public:
    static CLogger *Get (void) { static CLogger Instance; return &Instance; }
    void Write (const char *, TLogSeverity, const char *, ...) {}
};

#endif
