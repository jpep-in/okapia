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
    if (++s_nSeconds <= 3 || (s_nSeconds % 15) == 0)
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
    static unsigned s_nLastReads = 0;
    if (g_nTimebaseReads != s_nLastReads)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "guest timebase reads: %u",
                                g_nTimebaseReads);
        s_nLastReads = g_nTimebaseReads;
    }
#endif

    // Sound only says anything once the Mac has a source playing, which is
    // exactly when you want to see whether blocks are getting through.
    if ((s_nSeconds % 5) == 0)
    {
        AudioReport (s_nSeconds);
    }

    // XPRAM is written back by the kernel when it changes, not on a timer
    // (plan §7.8), so there is nothing periodic to do for it here.
}

static void OneTick (void)
{
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

static void PeriodicHandler (void)
{
    if (!s_bRunning || tick_inhibit)
    {
        return;
    }

    s_nAccumulator += HOST_TICK_USEC;
    while (s_nAccumulator >= MAC_TICK_USEC)
    {
        s_nAccumulator -= MAC_TICK_USEC;
        OneTick ();
    }
}

void TickInit (void)
{
    s_nAccumulator = 0;
    s_nTickCounter = 0;
    s_bRunning = false;

    // Once for the life of the board, never once per start. Circle keeps four
    // periodic slots and offers no way to give one back, so a registration at
    // each start asserts on the fifth (timer.cpp:637) — and an assertion halts,
    // which under QEMU ends the session outright. That is four restarts from Mac
    // OS and then a machine that dies at the next one, seemingly at random.
    // Everything this handler needs is reset above, and s_bRunning gates it.
    static bool s_bArmed;
    if (!s_bArmed)
    {
        CTimer::Get ()->RegisterPeriodicHandler (PeriodicHandler);
        s_bArmed = true;
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Tick armed: host %u Hz, Mac tick every %u us",
                            (unsigned) HZ, MAC_TICK_USEC);
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
