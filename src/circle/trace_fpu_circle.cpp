/*
 * trace_fpu_circle.cpp — how much the Macintosh actually uses its FPU.
 *
 * The question this answers is the one that decides whether the floating-point
 * core is worth an argument. A conversion test proves the arithmetic is right;
 * it says nothing about whether any Macintosh ever runs it. Counting the
 * instructions does — and it is the difference between "the round trip is
 * lossless" and "the round trip is lossless and the guest takes it".
 *
 * Hooked at link time rather than by patching external/ (docs/contributing/testing-and-debugging.md), so the
 * spelling is __wrap_ plus the mangled name and nothing more.
 *
 * Off unless src/kernel/Makefile passes OKAPIA_TRACE=1.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "okapia_circle.h"

#define FROM "okapia-fpu"

static unsigned s_nArithmetic;
static unsigned s_nLastReport;

static void Count (void)
{
    s_nArithmetic++;

    // The first few say the path is alive at all; after that, once per five
    // seconds and only while the number is moving.
    if (s_nArithmetic <= 3)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "FPU instruction %u", s_nArithmetic);
        return;
    }

    static unsigned s_nAtLastReport;
    const unsigned nNow = CTimer::Get ()->GetTicks () / HZ;
    if (nNow != s_nLastReport && (nNow % 5) == 0 && s_nArithmetic != s_nAtLastReport)
    {
        s_nLastReport   = nNow;
        s_nAtLastReport = s_nArithmetic;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "%u FPU instructions in %u s", s_nArithmetic, nNow);
    }
}

// fpuop_arithmetic(uae_u32 opcode, uae_u32 extra), which is every FMOVE, FADD,
// FSIN and the rest — the whole of the 68881's work except the branch and the
// state frames.
extern "C" void __real__Z16fpuop_arithmeticjj (uint32 opcode, uint32 extra);

extern "C" void __wrap__Z16fpuop_arithmeticjj (uint32 opcode, uint32 extra)
{
    Count ();
    __real__Z16fpuop_arithmeticjj (opcode, extra);
}
