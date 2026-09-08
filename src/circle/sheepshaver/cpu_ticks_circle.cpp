/*
 * cpu_ticks_circle.cpp — the PowerPC engine's periodic seam.
 *
 * The 68k engine gets one for free: uae_cpu counts opcodes and calls
 * cpu_do_check_ticks() when the counter wraps. kpx_cpu has nothing of the kind,
 * because every host it was written for had a thread to spare — SheepShaver's
 * tick, its PRAM writer and its input all live in one, beside the emulator.
 *
 * Okapia has none. Its 60 Hz tick is a Circle timer interrupt on core 0, where
 * blocking on the SD card is not allowed, and the interpreter on the other core
 * never returns until the Macintosh stops. So without a seam inside the loop
 * there is no moment at all in which this engine may write to the card — which
 * is why the PowerPC Macintosh had no PRAM persistence before this file
 * existed, and why its input reached adb.cpp from the wrong core.
 *
 * patches/macemu/0004 adds the call site; this is what it calls. Everything
 * here runs on the emulation core, between two PowerPC instructions.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include "cpu_emulation.h"
#include "main.h"

#define FROM "okapia-ppc"

// input_circle.cpp — the USB handlers on core 0 only record; this hands the
// events to adb.cpp from the core that reads them.
extern void InputDrain (void);

// xpram_circle.cpp — compares the Mac's parameter RAM with what is on the card
// and writes when they differ. The 68k engine is told the moment it changes
// (emul_op_hook_circle.cpp); SheepShaver's ROM has no equivalent opcode, so
// here it is polled — 256 bytes, at the rate below.
extern void XPRAMWatchdog (void);

// One PRAM comparison every this many visits. The drain has to be prompt; the
// watchdog only has to beat the next power cut, and a memcmp on the emulation
// core is not free.
static const unsigned PRAM_EVERY = 64;

static unsigned s_nVisits;
static unsigned s_nLastReport;

/*
 *  mac_encoding_circle.cpp asks before running 68k code on the guest. This
 *  engine never lets it: SheepShaver's 68k is the Toolbox's, reached through a
 *  nanokernel, and re-entering it from inside an ExtFS callback is not a thing
 *  upstream ever does here. Answering no costs the Japanese-System check and
 *  leaves the MacRoman conversion, which is the right guess on this hardware.
 */
bool MacIsExecuting (void)
{
    return false;
}

void powerpc_check_ticks (void)
{
    s_nVisits++;

    InputDrain ();

    if ((s_nVisits % PRAM_EVERY) == 0)
    {
        XPRAMWatchdog ();
    }

    // PPC_CHECK_TICKS is a count of instructions, so the rate it produces
    // depends on how fast this board interprets — which is exactly the number
    // nobody can guess. Report it rather than assume it: a seam that fires
    // twice a second is not a seam, and this says so in five seconds.
    unsigned nNow = CTimer::Get ()->GetTicks () / HZ;
    if (nNow != s_nLastReport && nNow != 0 && (nNow % 5) == 0)
    {
        s_nLastReport = nNow;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "periodic seam: %u visits in %u s (%u Hz)",
                                s_nVisits, nNow, s_nVisits / nNow);
    }
}
