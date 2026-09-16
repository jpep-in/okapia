//
// exctest.cpp — does C++ exception unwinding actually work under Circle?
//
// Basilisk II's core reports 68k bus and address errors by throwing a C++
// exception (the upstream default) or by longjmp (EXCEPTIONS_VIA_LONGJMP, a path
// no upstream platform enables, hence one nobody tests). Choosing between them
// should rest on a measurement, not on a guess — this is that measurement.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "exctest.h"

// Mirrors the shape of uae_cpu_2021/memory.h: a small struct carrying the 68k
// exception number, thrown across a couple of frames.
struct m68k_exception
{
    int prb;
    explicit m68k_exception (int exc) : prb (exc) {}
    operator int () { return prb; }
};

static void ThrowDeep (int nDepth)
{
    if (nDepth == 0)
    {
        throw m68k_exception (2);      // 2 = bus error, as the core would
    }
    ThrowDeep (nDepth - 1);
}

int TestCppException (void)
{
    try
    {
        ThrowDeep (3);                 // unwinds through several frames
    }
    catch (m68k_exception var)
    {
        return var;                    // expect 2
    }
    return -1;
}
