/*
 * trace_disk_circle.cpp — diagnostic wrapper around Sys_read.
 *
 * Logs every block the guest asks for and flags short reads, which are what
 * DiskPrime turns into readErr (disk.cpp:337). This is how you tell a failing
 * read from a Mac that simply stops asking — the difference between a broken
 * file layer and a broken volume, and it found a corrupt HFS image once already.
 * The linker's --wrap gives the hook without touching external/ (AGENTS.md).
 *
 * Off unless src/kernel/Makefile passes OKAPIA_TRACE=1; see the LDFLAGS block in
 * that file.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

// Sys_write(void *, void *, loff_t, size_t). Writes are the ones that decide
// whether an interrupted session leaves a repairable volume or a wrecked one,
// so they are logged in full: offset, length, and what actually got written.
extern "C" size_t __real__Z9Sys_writePvS_lm (void *fh, void *buffer,
                                             loff_t offset, size_t length);

static unsigned s_nWrites;
static unsigned s_nShortWrites;

extern "C" size_t __wrap__Z9Sys_writePvS_lm (void *fh, void *buffer,
                                             loff_t offset, size_t length)
{
    size_t nActual = __real__Z9Sys_writePvS_lm (fh, buffer, offset, length);

    if (nActual != length)
    {
        s_nShortWrites++;
    }

    // Every write speaks for the first 400, then only the failures and a
    // periodic total: a busy guest writes thousands.
    if (nActual != length || s_nWrites < 400 || (s_nWrites % 500) == 0)
    {
        CLogger::Get ()->Write ("okapia-disk", LogNotice,
                                "write #%u: block %lu, %u bytes -> %u%s (short so far: %u)",
                                s_nWrites,
                                (unsigned long) (offset / 512),
                                (unsigned) length, (unsigned) nActual,
                                nActual != length ? "  SHORT WRITE" : "",
                                s_nShortWrites);
    }

    s_nWrites++;
    return nActual;
}

// Sys_read(void *, void *, loff_t, size_t), as mangled by the AArch64 ABI.
extern "C" size_t __real__Z8Sys_readPvS_lm (void *fh, void *buffer,
                                            loff_t offset, size_t length);

static unsigned s_nReads;

extern "C" size_t __wrap__Z8Sys_readPvS_lm (void *fh, void *buffer,
                                            loff_t offset, size_t length)
{
    size_t nActual = __real__Z8Sys_readPvS_lm (fh, buffer, offset, length);

    // Every short read becomes readErr in DiskPrime (disk.cpp:337), so those
    // always speak. The rest are capped: a real boot reads thousands of blocks.
    if (nActual != length || s_nReads < 400)
    {
        CLogger::Get ()->Write ("okapia-disk", LogNotice,
                                "read #%u: block %lu (offset %lu), %u bytes -> %u%s",
                                s_nReads,
                                (unsigned long) (offset / 512),
                                (unsigned long) offset,
                                (unsigned) length, (unsigned) nActual,
                                nActual != length ? "  SHORT READ" : "");
    }

    s_nReads++;
    return nActual;
}
