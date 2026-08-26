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

void cpu_do_check_ticks (void)
{
    // Nothing periodic belongs here: interrupts arrive from the timer on core 0.
    // Emulation runs on a secondary core with no scheduler, so there is nothing
    // to yield to either. Kept as the hook the interpreter expects.
}
