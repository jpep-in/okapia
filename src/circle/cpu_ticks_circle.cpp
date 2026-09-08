/*
 * cpu_ticks_circle.cpp — pacing hooks the 68k interpreter calls.
 *
 * The interpreter counts down emulated_ticks and calls cpu_do_check_ticks() when
 * it wraps, which is where a host gets to do periodic work. On Okapia the 60 Hz
 * tick comes from a Circle timer interrupt instead (plan §7.3), so the timing
 * itself must not happen here.
 *
 * What does belong here is everything that has to run *in the 68k thread*. The
 * tick handler runs at IRQ level on core 0, where blocking on the card is not
 * allowed and where adb.cpp's unlocked key ring is on the wrong side of a
 * memory barrier; this hook runs on the emulation core, between two
 * instructions, about every 65 536 opcodes — some 4 ms, a quarter of a Mac
 * tick. It is the cheapest correct place there is, and it was a counter.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "cpu_emulation.h"
#include "main.h"

// Counts up and wraps; the interpreter calls us on overflow (newcpu.h:332).
// Preloading it after each visit is what decides how many opcodes until the
// next one, and that is the whole of the calibration below.
uint16 emulated_ticks;

/*
 *  A seam armed by the clock, not by an opcode count
 *
 *  Left to overflow from zero the hook comes round every 65536 opcodes, which
 *  is a wall-clock rate only if the engine's speed is fixed — and it is not.
 *  Measured under QEMU: 209 visits in one five-second window and 1076 in the
 *  next, so the pointer was handed to the Macintosh anywhere between every 5
 *  and every 24 ms, the slow end being slower than the screen refresh it is
 *  supposed to feed. The comment this replaces claimed four milliseconds.
 *
 *  So the quantum is measured against the clock and corrected, which is what
 *  infinite-mac does for the same reason — it is the other port with no thread
 *  to spare (main_unix.cpp:342, "Recalibrate 1000 Hz quantum every 10 ticks").
 *  A kilohertz costs a handful of atomic loads per visit and buys a pointer
 *  whose sampling rate no longer depends on what the guest is doing.
 */
static const unsigned TARGET_USEC   = 1000;
static const unsigned QUANTUM_MIN   = 256;
static const unsigned QUANTUM_MAX   = 65536;
static const unsigned CALIBRATE_EVERY = 64;     // visits

static unsigned s_nQuantum = 8192;
static unsigned s_nSinceCalibration;
static unsigned s_nCalibratedAt;

// Opcodes retired, accumulated with the quantum that was in force: multiplying
// a visit count by the current quantum would rewrite history at every
// correction.
static u64 s_nOpcodes;

static void ArmNextVisit (void)
{
    s_nOpcodes += s_nQuantum;

    if (++s_nSinceCalibration >= CALIBRATE_EVERY)
    {
        const unsigned nNow = CTimer::GetClockTicks ();
        if (s_nCalibratedAt != 0)
        {
            const unsigned nPer = (nNow - s_nCalibratedAt) / s_nSinceCalibration;
            if (nPer != 0)
            {
                // Proportional, and deliberately not a single step: the engine's
                // speed changes with what the Macintosh is running, and a
                // quantum that chases every window oscillates.
                unsigned nWanted = (unsigned)
                    ((u64) s_nQuantum * TARGET_USEC / nPer);
                nWanted = (s_nQuantum + nWanted) / 2;
                if (nWanted < QUANTUM_MIN) nWanted = QUANTUM_MIN;
                if (nWanted > QUANTUM_MAX) nWanted = QUANTUM_MAX;
                s_nQuantum = nWanted;
            }
        }
        s_nCalibratedAt = nNow;
        s_nSinceCalibration = 0;
    }

    // The interpreter calls us when this wraps to zero, so the two's complement
    // of the quantum is the count of opcodes until the next visit.
    emulated_ticks = (uint16) (0u - s_nQuantum);
}

// Set while the Mac must not be interrupted, e.g. during a mode switch.
bool tick_inhibit;

/*
 *  The interpreter calls this when emulated_ticks wraps, so roughly every 65536
 *  opcodes. Interrupts still come from the timer on core 0; what runs here is
 *  what needs the 68k thread. It is also the one place that proves the 68k is
 *  executing at all, and it costs a counter to say so.
 */

static unsigned s_nLastReport;

/*
 *  Is there a 68000 executing right now?
 *
 *  mac_encoding_circle.cpp asks the Macintosh which alphabet it writes in, and
 *  asking means running 68k code. ExtFSInit() converts the shared volume's name
 *  before the processor has started, so something has to say when it is safe;
 *  this hook runs only from inside the interpreter, which makes it the answer.
 */
static bool s_bExecuting;

bool MacIsExecuting (void)
{
    return s_bExecuting;
}

// input_circle.cpp — the USB handlers only record; this is where the events
// reach the Macintosh, on the core that reads them.
extern void InputDrain (void);

// rsrc_patches.cpp (patches/macemu/0005) and main_circle.cpp. Between them they
// answer the question a boot log cannot otherwise settle: is the Macintosh
// still starting, or has it started and gone quiet?
extern bool HasIdleTime (void);
extern bool MacHasBeenIdle (void);

void cpu_do_check_ticks (void)
{
    s_bExecuting = true;

    ArmNextVisit ();
    InputDrain ();

    // Same reason as InputReport(): a thousand visits a second must not mean a
    // thousand clock reads for a line printed every five seconds.
    static unsigned s_nUntilLook;
    if (s_nUntilLook-- != 0)
    {
        return;
    }
    s_nUntilLook = 256;

    unsigned nNow = CTimer::Get ()->GetTicks () / HZ;      // seconds since boot
    if (nNow != s_nLastReport && (nNow % 5) == 0)
    {
        s_nLastReport = nNow;

        // Several cycles per opcode: this is an opcode rate, not a MIPS figure.
        u64 nOpcodes = s_nOpcodes;
#if DIRECT_ADDRESSING_GUARD
        // main_circle.cpp. The guard itself may not log; this is where it does.
        extern void GuestBoundsReport (void);
        GuestBoundsReport ();
#endif

        CLogger::Get ()->Write ("okapia-68k", LogNotice,
                                "running: %lu k opcodes in %u s (%lu k/s), "
                                "quantum %u, %s",
                                (unsigned long) (nOpcodes / 1000), nNow,
                                (unsigned long) (nOpcodes / 1000 / (nNow ? nNow : 1)),
                                s_nQuantum,
                                MacHasBeenIdle () ? "started"
                                : HasIdleTime ()  ? "still starting"
                                                  : "no idle patch: cannot tell");
    }
}
