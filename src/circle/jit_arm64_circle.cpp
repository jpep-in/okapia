/*
 * jit_arm64_circle.cpp — what the 68k to AArch64 translator asks of the board
 * (OKAPIA_JIT_ARM64=1, experimental).
 *
 * The translator itself is upstream-shaped code in the 68k core
 * (patches/macemu/0012, uae_cpu_2021/jit_arm64/). It asks its host for two
 * things, and this file is both:
 *
 *   memory   jit_arm64_host_alloc_code — a block it can write code into and
 *            run. Circle marks the heap non-executable; execmem_circle.cpp
 *            clears that for the pages of one block, once.
 *   time     jit_arm64_host_poll — called every time translated code comes
 *            back to the execution loop. The interpreter reaches its seam
 *            (cpu_do_check_ticks) by counting opcodes; translated code retires
 *            no opcode the loop can count, so the seam is reached by the clock.
 *
 * Without OKAPIA_JIT_ARM64 the file is empty.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifdef JIT_ARM64

#include "sysdeps.h"
#include "okapia_circle.h"
#include "execmem_circle.h"
#include "jit_arm64/jit_arm64.h"

// The interpreter's seam (cpu_ticks_circle.cpp). Declared, not included: newcpu.h
// wants half the core's headers ahead of it.
extern void cpu_do_check_ticks (void);

#include <stdlib.h>

static const char FROM[] = "jit";

/*
 *  The code block
 *
 *  Claimed once for the life of the board: the translator calls this the first
 *  time a Macintosh starts with "jit true" and keeps the block across restarts
 *  (jit_arm64_init forgets the translations, not the memory). It comes from the
 *  heap after the Mac RAM block, which is claimed first, so a 1 GB board keeps
 *  its budget; and it is not below the Mac RAM, so a stray guest address can
 *  reach it exactly as it can reach anything else here (docs/topics/engine-68k.md).
 */
static const size_t PAGE = 0x10000;

void *jit_arm64_host_alloc_code (size_t size)
{
    size = (size + PAGE - 1) & ~(PAGE - 1);

    uint8 *block = (uint8 *) malloc (size + PAGE);
    if (block == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No memory for %u MB of translated code",
                                (unsigned) (size >> 20));
        return 0;
    }
    uint8 *code = (uint8 *) (((uintptr) block + PAGE - 1) & ~(uintptr) (PAGE - 1));
    if (!ExecMemMakeExecutable (code, size))
    {
        CLogger::Get ()->Write (FROM, LogError, "The code block could not be made executable");
        free (block);
        return 0;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "68k translator: %u MB of code at %lx, executable",
                            (unsigned) (size >> 20), (unsigned long) code);
    return code;
}

/*
 *  The seam, by the clock
 *
 *  A kilohertz, the rate cpu_ticks_circle.cpp calibrates the interpreter to.
 *  Translated code returns here at least once every few thousand backward
 *  branches, which is microseconds, so the clock is looked at often enough;
 *  it is a register read (CNTPCT_EL0), not a call into Circle's timer.
 */
void jit_arm64_host_poll (void)
{
    static unsigned s_nLastVisit;

    const unsigned nNow = CTimer::GetClockTicks ();
    if (nNow - s_nLastVisit >= 1000)
    {
        s_nLastVisit = nNow;
        cpu_do_check_ticks ();
    }
}

#endif
