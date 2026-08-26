/*
 * cpu_ticks_circle.cpp — pacing hooks the 68k interpreter calls.
 *
 * The interpreter counts down emulated_ticks and calls cpu_do_check_ticks() when
 * it wraps, which is where a host gets to do periodic work. On Okapia the 60 Hz
 * tick comes from a Circle timer interrupt instead (plan §7.3), so this is where
 * the emulation loop merely yields — it must not do the timing itself.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "cpu_emulation.h"
#include "main.h"

// Counts up and wraps; the interpreter calls us on overflow (newcpu.h:329).
uint16 emulated_ticks;

// Set while the Mac must not be interrupted, e.g. during a mode switch.
bool tick_inhibit;

/*
 *  The interpreter calls this when emulated_ticks wraps, so roughly every 65536
 *  opcodes. Nothing periodic belongs here — interrupts come from the timer on
 *  core 0 — but it is the one place that proves the 68k is executing at all, and
 *  it costs a counter to say so.
 */

static unsigned s_nWraps;
static unsigned s_nLastReport;

void cpu_do_check_ticks (void)
{
    s_nWraps++;

    unsigned nNow = CTimer::Get ()->GetTicks () / HZ;      // seconds since boot
    if (nNow != s_nLastReport && (nNow % 5) == 0)
    {
        s_nLastReport = nNow;

        // 65536 opcodes per wrap; the guest executes several cycles per opcode,
        // so this is an opcode rate, not a MIPS figure.
        u64 nOpcodes = (u64) s_nWraps * 65536;
        CLogger::Get ()->Write ("okapia-68k", LogNotice,
                                "running: %lu k opcodes in %u s (%lu k/s)",
                                (unsigned long) (nOpcodes / 1000), nNow,
                                (unsigned long) (nOpcodes / 1000 / (nNow ? nNow : 1)));
    }
}
