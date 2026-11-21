/*
 * execmem_circle.h — a self-test for executable memory.
 *
 * Circle marks every page above _etext non-executable (PXN = 1), the heap
 * included. A 68k JIT needs a region of heap it can write code into and run.
 * ExecMemSelfTest proves, once, that our own code can clear PXN on one 64 KB
 * page and run, then re-emit and run again, instruction words written there.
 * Not a JIT, and it patches nothing in Circle: it walks the table Circle built.
 *
 * Shared by both engines (SHARED_SRCS): it reads the one translation table the
 * board has, so two copies would be two claims on it. The self-test is built
 * only with EXECMEM_SELFTEST (OKAPIA_TRACE=1); ExecMemMakeExecutable() always,
 * and the 68k JIT's arena is its one caller (jit_circle.cpp, OKAPIA_JIT=1).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_EXECMEM_CIRCLE_H
#define OKAPIA_EXECMEM_CIRCLE_H

#include <stddef.h>

// Clears PXN on every 64 KB page from pStart to pStart + nLength, both of which
// must lie on a 64 KB boundary, and flushes their TLB entries. False, with a
// line in the log, when the range is not aligned or not mapped by level 3
// pages. The pages stay writable: Circle clears SCTLR_EL1.WXN.
bool ExecMemMakeExecutable (void *pStart, size_t nLength);

// Once at startup, after the serial log exists. Logs one PASS or FAIL line and
// always returns. Empty without EXECMEM_SELFTEST.
void ExecMemSelfTest (void);

#endif
