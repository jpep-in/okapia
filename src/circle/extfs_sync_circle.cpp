/*
 * extfs_sync_circle.cpp — the shared folder's flush policy.
 *
 * FatFs keeps the tail of a write in the file object and only records the new
 * size in the directory entry at f_sync or f_close. For the disk image that
 * window does not exist: every write the Mac makes is a whole number of
 * 512-byte sectors at a sector boundary, which FatFs sends straight to the
 * card. The shared folder writes whatever length the guest asked for, so it
 * does open one — and docs/topics/storage.md says a change that adds a
 * write-back cache owes a flush policy in the same change. This is it.
 *
 * A pulled plug during a copy then costs the file being copied and nothing
 * else: what was acknowledged is on the card, and the directory entry agrees
 * with it. Without this, the entry can still say zero bytes for a file whose
 * clusters are already written, which is worse than losing the copy — it looks
 * like success.
 *
 * Hooked at link time rather than by patching external/ (docs/contributing/guide.md). Circle
 * invokes ld directly, so the flag is a bare --wrap.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include <unistd.h>

// The wrapped name is the mangled one, and the wrapper must carry it verbatim
// — so both are extern "C", as in trace_disk_circle.cpp.
extern "C" ssize_t __real__Z11extfs_writeiPvm (int fd, void *buffer, size_t length);

extern "C" ssize_t __wrap__Z11extfs_writeiPvm (int fd, void *buffer, size_t length)
{
    ssize_t nWritten = __real__Z11extfs_writeiPvm (fd, buffer, length);
    if (nWritten > 0)
    {
        // Failure here is not the caller's to act on — the bytes are written
        // either way — but it means the card did not confirm, so say so.
        if (fsync (fd) != 0)
        {
            CLogger::Get ()->Write ("okapia-extfs", LogWarning,
                                    "fsync failed after %d bytes", (int) nWritten);
        }
    }
    return nWritten;
}
