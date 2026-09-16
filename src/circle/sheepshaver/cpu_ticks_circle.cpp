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
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "hal_circle.h"

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

// The lifetime average hid the whole problem once: 87 Hz over a run whose last
// window was 116. What the pointer is given is the rate now, not the average.
static unsigned s_nVisitsAtReport;

/*
 *  The seam's own cadence, measured rather than assumed
 *
 *  PPC_CHECK_TICKS is a count of interpreted instructions, and a count of
 *  instructions is a rate only on a machine whose speed never varies. This
 *  one's does: at the fixed 50000 the seam was measured at 87 Hz averaged over
 *  a run and 116 Hz over its last window, so the pointer reached the Macintosh
 *  at a rate that followed what the Macintosh happened to be doing.
 *
 *  patches/macemu/0004 therefore compares against this variable rather than the
 *  constant, and the constant becomes its starting value. The arithmetic below
 *  is the 68k engine's, deliberately unchanged (cpu_ticks_circle.cpp): the two
 *  seams have the same job and there is no reason for them to drift apart.
 *
 *  A kilohertz for the same reason as there: a USB mouse reports at about
 *  100 Hz, and a drain that is not comfortably faster than its input merges
 *  reports. It costs less here than on the 68k, since this engine hands the
 *  Macintosh a position rather than a delta and a merged report is only late,
 *  never wrong -- but late is what a pointer is judged on.
 */
uint32 ppc_check_ticks_quantum = PPC_CHECK_TICKS;

static const unsigned TARGET_USEC     = 1000;
static const unsigned QUANTUM_MIN     = 2000;
static const unsigned QUANTUM_MAX     = 4000000;
static const unsigned CALIBRATE_EVERY = 64;     // visits

static unsigned s_nSinceCalibration;
static unsigned s_nCalibratedAt;

static void Recalibrate (void)
{
    if (++s_nSinceCalibration < CALIBRATE_EVERY)
    {
        return;
    }

    const unsigned nNow = CTimer::GetClockTicks ();
    if (s_nCalibratedAt != 0)
    {
        const unsigned nPer = (nNow - s_nCalibratedAt) / s_nSinceCalibration;
        if (nPer != 0)
        {
            // Proportional and damped: the interpreter's speed changes with
            // what the guest is running, and a quantum that chases every
            // window oscillates instead of settling.
            unsigned nWanted = (unsigned)
                ((u64) ppc_check_ticks_quantum * TARGET_USEC / nPer);
            nWanted = (ppc_check_ticks_quantum + nWanted) / 2;
            if (nWanted < QUANTUM_MIN) nWanted = QUANTUM_MIN;
            if (nWanted > QUANTUM_MAX) nWanted = QUANTUM_MAX;
            ppc_check_ticks_quantum = nWanted;
        }
    }
    s_nCalibratedAt = nNow;
    s_nSinceCalibration = 0;
}

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

    Recalibrate ();
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
        // Here and not in the tick: asking the firmware and writing the card
        // both block.
        const bool bReport = PerfReportWanted ();
        BoardWatchClock (bReport);
        if (bReport)
        {
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "periodic seam: %u visits in %u s (%u Hz "
                                    "lifetime, %u Hz now), quantum %u",
                                    s_nVisits, nNow, s_nVisits / nNow,
                                    (s_nVisits - s_nVisitsAtReport) / 5,
                                    (unsigned) ppc_check_ticks_quantum);
        }
        s_nVisitsAtReport = s_nVisits;
        BoardLogFlush ();
    }
}
