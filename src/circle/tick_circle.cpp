/*
 * tick_circle.cpp — the Mac's heartbeat.
 *
 * The 60 Hz VBL interrupt is what makes Mac OS advance: without it the ROM runs
 * but waits on time that never passes. Upstream drives it from a pthread that
 * sleeps 16625 µs at a time; Circle's scheduler is cooperative and cannot
 * preempt the 68k loop, so it has to come from a timer interrupt instead
 * (plan §7.3). The handler runs on core 0 in IRQ context and does nothing but
 * set flags — the emulation itself runs on another core.
 *
 * Circle's periodic handler fires at HZ, which is 100. The Mac wants 60.15 Hz,
 * so an accumulator emits a Mac tick once enough host ticks have passed. That
 * gives the right average rate with up to 10 ms of jitter, which the VBL
 * tolerates; audio and tear-free video will later want the display's own VSync
 * rather than this.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "hal_circle.h"

#include <stdio.h>
#include <string.h>


#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "timer.h"
#include "xpram.h"
#include "video.h"

#define FROM "okapia-tick"

// Set while the Mac must not be interrupted; defined in cpu_ticks_circle.cpp.
extern bool tick_inhibit;

static unsigned s_nAccumulator;     // host ticks scaled by 1000, see below
static unsigned s_nTickCounter;     // Mac ticks, for the one-per-second work
static bool     s_bRunning;
// PerfReportWanted(), read at TickInit(): what follows runs in the tick
// interrupt, which is no place to walk the preferences.
static bool     s_bReport;

/*
 *  Measurement (temporary): is the Mac's vertical blank evenly spaced?
 *
 *  PeriodicHandler() below emits Mac ticks from an accumulator driven at HZ,
 *  so the average rate is exact by construction and the spacing need not be.
 *  The guest redraws its pointer on this beat, and the eye is given the
 *  spacing, not the average — so the average is not the thing to report.
 */
static unsigned s_nLastTick;            // us, free-running counter
static unsigned s_TickGaps[40];         // one bucket per ms; 39 catches the rest
static unsigned s_nGapMin, s_nGapMax;

static void TickMeasure (void)
{
    const unsigned nNow = CTimer::GetClockTicks ();
    if (s_nLastTick != 0)
    {
        const unsigned nGap = nNow - s_nLastTick;
        const unsigned nMs  = nGap / 1000;
        s_TickGaps[nMs < 40 ? nMs : 39]++;
        if (s_nGapMin == 0 || nGap < s_nGapMin) s_nGapMin = nGap;
        if (nGap > s_nGapMax)                   s_nGapMax = nGap;
    }
    s_nLastTick = nNow;
}

static void TickReport (void)
{
    char Line[160];
    unsigned nAt = 0;
    Line[0] = '\0';
    for (unsigned i = 0; i < 40; i++)
    {
        if (s_TickGaps[i] != 0 && nAt + 16 < sizeof Line)
        {
            nAt += (unsigned) snprintf (Line + nAt, sizeof Line - nAt, "%s%u:%u",
                                        nAt != 0 ? " " : "", i, s_TickGaps[i]);
        }
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                            "VBL spacing: min %u us, max %u us, ms:count %s",
                            s_nGapMin, s_nGapMax, Line);
    memset (s_TickGaps, 0, sizeof s_TickGaps);
    s_nGapMin = 0;
    s_nGapMax = 0;
}

// A Mac tick is 16625 µs; a host tick is 1000000/HZ µs. Working in microseconds
// keeps the average exact instead of drifting a few seconds per hour.
static const unsigned MAC_TICK_USEC  = 16625;
static const unsigned HOST_TICK_USEC = 1000000 / HZ;

// audio_circle.cpp: AudioPump raises INTFLAG_AUDIO while the sound queue has
// room, so the 68k side fetches the next block. Nothing here runs 68k code.
extern void AudioPump (void);
extern void AudioReport (unsigned nSeconds);

static void OneSecond (void)
{
    // INTFLAG_1HZ is what drives DiskInterrupt(), and therefore volume mounting
    // (emul_op.cpp:484). Worth confirming it actually fires.
    static unsigned s_nSeconds;
    if (++s_nSeconds <= 3 || (s_bReport && (s_nSeconds % 15) == 0))
    {
        // Do the Mac's two clocks agree? Ticks (low memory 0x16A) is bumped by
        // our 60 Hz VBL; Microseconds() comes from Circle's counter. If they
        // drift apart, a delay the Mac waits on may never appear to elapse.
        uint32 hi, lo;
        Microseconds (hi, lo);
        uint64 us = ((uint64) hi << 32) | lo;
        uint32 macTicks = ReadMacInt32 (0x16A);
        CLogger::Get ()->Write (FROM, LogNotice,
                                "1 Hz #%u: Mac Ticks %u (expect ~%u), Microseconds %lu s",
                                s_nSeconds, (unsigned) macTicks, s_nSeconds * 60,
                                (unsigned long) (us / 1000000));
    }

    // The two engines number their flags differently, and SheepShaver has no
    // one-second interrupt at all: its ROM drives the disk from the 60 Hz one.
    // Keeping the test rather than pruning it is what §3.5 asks for.
#ifndef SHEEPSHAVER
    SetInterruptFlag (INTFLAG_1HZ);
#else
    // Bring-up diagnostic, and the cheapest one there is: the nanokernel reads
    // the timebase constantly, so a count that stops moving says the guest has
    // stopped executing rather than executed something wrong. Silent once it
    // settles, so it costs nothing on a machine that works.
    extern volatile unsigned g_nTimebaseReads;
    extern uint32 gStrayCount, gStrayFirst;
    static unsigned s_nLastReads = 0;
    static uint32   s_nLastStray = 0;
    if (s_bReport
        && (g_nTimebaseReads != s_nLastReads || gStrayCount != s_nLastStray))
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "guest: %u timebase reads, %u accesses outside "
                                "the block, first at 0x%08X",
                                g_nTimebaseReads, (unsigned) gStrayCount,
                                (unsigned) gStrayFirst);
        s_nLastReads = g_nTimebaseReads;
        s_nLastStray = gStrayCount;
    }
#endif

    // Sound only says anything once the Mac has a source playing, which is
    // exactly when you want to see whether blocks are getting through.
    if (s_bReport && (s_nSeconds % 5) == 0)
    {
        AudioReport (s_nSeconds);
        TickReport ();
    }

    // XPRAM is written back by the kernel when it changes, not on a timer
    // (docs/topics/clock-and-pram.md), so there is nothing periodic to do for it here.
}

static void OneTick (void)
{
    TickMeasure ();

    if (++s_nTickCounter > 60)
    {
        s_nTickCounter = 0;
        OneSecond ();
    }

#ifdef SHEEPSHAVER
    SetInterruptFlag (INTFLAG_VIA);
#else
    SetInterruptFlag (INTFLAG_60HZ);
#endif
    TriggerInterrupt ();

    AudioPump ();
}

/*
 *  Why the coarse timer is not enough
 *
 *  Circle's periodic handler fires at HZ, which is 100, so a Mac tick can only
 *  be emitted on a 10 ms grid — and 16625 does not divide 10000. The spacing is
 *  20, 10, 20, 20, 10 ms: the average is exact, one frame in three is half as
 *  long as its neighbours, and the Macintosh redraws its pointer on that beat.
 *  Measured invariant at 103 short intervals to 202 long ones in every
 *  five-second window.
 *
 *  The fine timer that fixes it is the board's, not this file's: hal_circle.cpp
 *  holds the claim, because this file is compiled once per engine and a claim
 *  guarded per engine is claimed twice the moment the two Macintoshes trade
 *  places. See BoardFineTick().
 */
static void FineTick (void)
{
    if (s_bRunning && !tick_inhibit)
    {
        OneTick ();
    }
}

static void PeriodicHandler (void)
{
    if (!s_bRunning || tick_inhibit)
    {
        return;
    }

    s_nAccumulator += HOST_TICK_USEC;
    if (s_nAccumulator >= MAC_TICK_USEC)
    {
        s_nAccumulator -= MAC_TICK_USEC;
        if (s_nAccumulator >= MAC_TICK_USEC)
        {
            // More than a tick owed. Circle rearms its own compare register by
            // one period whatever the delay (timer.cpp:577), so a late
            // interrupt fires again at once and calls us twice in a row; adding
            // our own catch-up on top is what turned a late tick into a burst.
            s_nAccumulator = 0;
        }
        OneTick ();
    }
}

void TickInit (void)
{
    s_nAccumulator = 0;
    s_nTickCounter = 0;
    s_bRunning = false;
    s_bReport = PerfReportWanted ();
    if (!s_bReport)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Periodic reports off: the serial port is polled "
                                "and would stop the Macintosh (perfreport)");
    }

    // Once for the life of the board, never once per start. Circle keeps four
    // periodic slots and offers no way to give one back, so a registration at
    // each start asserts on the fifth (timer.cpp:637) — and an assertion halts,
    // which under QEMU ends the session outright. That is four restarts from Mac
    // OS and then a machine that dies at the next one, seemingly at random.
    // Everything this handler needs is reset above, and s_bRunning gates it.
    // The board owns the fine timer and only swaps our handler in, so this is
    // safe to call at every start and from either engine. The periodic handler
    // is the fallback and *that* one still needs the guard: Circle keeps four
    // slots and gives none of them back.
    const bool bFine = BoardFineTick (MAC_TICK_USEC, FineTick);

    static bool s_bArmed;
    if (!bFine && !s_bArmed)
    {
        s_bArmed = true;
        CTimer::Get ()->RegisterPeriodicHandler (PeriodicHandler);
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Tick armed: %s, Mac tick every %u us",
                            bFine ? "system timer, microsecond deadline"
                                  : "periodic handler on the host tick grid",
                            MAC_TICK_USEC);
}

void TickStart (void)
{
    // Held back until the emulator is ready: an interrupt raised before the Mac
    // can service it is lost, and a lost first VBL is hard to tell from a hang.
    s_bRunning = true;
}

void TickStop (void)
{
    s_bRunning = false;
}
