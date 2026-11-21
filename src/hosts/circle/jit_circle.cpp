/*
 * jit_circle.cpp — the platform under the 68k JIT (OKAPIA_JIT=1, experimental).
 *
 * The JIT is uae_cpu_2026 from rcarmo/macemu-jit, compiled unchanged. This file
 * is everything it asks of a platform:
 *
 *   memory   vm_acquire / vm_release / vm_protect — the code cache, the entry
 *            stubs and the block descriptors, all out of one arena claimed once
 *            and made executable (execmem_circle.cpp). Nothing is given back to
 *            the heap, so nothing is asked of it while the Macintosh runs.
 *   time     GetTicks_usec, jit_one_tick, TimerPoll — the JIT's dispatch loop
 *            drives the 60 Hz tick and the seam itself, at the one point where
 *            the guest's registers and flags are all in memory.
 *   the rest a tick thread there is none of, and two preferences.
 *
 * Without OKAPIA_JIT the file is empty.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifdef OKAPIA_JIT

#include "sysdeps.h"
#include "okapia_circle.h"
#include "execmem_circle.h"
#include "prefs.h"
#include "vm_alloc.h"

#include <stdlib.h>

static const char FROM[] = "jit";

/*
 *  The arena
 *
 *  One block for the life of the board. The translation cache is jitcachesize
 *  KB of it; the rest is the entry stubs and the pools of block descriptors,
 *  which LazyBlockAllocator takes 16 KB at a time while the Macintosh runs —
 *  out of here, so that is a pointer moving and not an allocation.
 *
 *  Everything the JIT emits must stay within a B's reach of its entry stubs
 *  (128 MB), which one arena guarantees.
 *
 *  Not yet below the Mac RAM block, where a stray guest address could not reach
 *  it (docs/notes/research/jit-68k.md): the Mac RAM is claimed first and the
 *  heap grows upward.
 */
static const size_t ARENA_PAGE  = 0x10000;
// Beyond the translation cache itself: the entry stubs and the pools of block
// descriptors and checksum records, which grow with the number of blocks alive
// and are taken 16 KB at a time. 32 MB of them ran out after 230 s of Speedometer
// on the Pi 4 ("Could not allocate block pool!", the Mac gone) once the cache
// held blocks instead of churning.
static const size_t ARENA_EXTRA = 256 * 0x100000;
static const size_t ALLOC_ALIGN = 4096;

static size_t s_nArenaSize;     // the cache the preferences ask for, plus the above

static uint8 *s_pArena;         // 64 KB aligned
static size_t s_nUsed;
static size_t s_nHighWater;
static unsigned s_nLive;        // acquisitions not yet released

static bool ArenaClaim (void)
{
    if (s_pArena != 0)
    {
        return true;
    }

    // The cache is jitcachesize KB. macemu-jit measured repeated hard flushes of
    // a full cache at 32768 KB and runs its desktop QA at 131072: so does the
    // Pi 4 (a 16384 KB cache was flushed 28 times in 81 s, 4.8 million blocks
    // compiled for a Speedometer run).
    size_t nCache = (size_t) PrefsFindInt32 ("jitcachesize") * 1024;
    if (nCache < 8 * 0x100000)
    {
        nCache = 8 * 0x100000;
    }
    s_nArenaSize = (nCache + ARENA_EXTRA + ARENA_PAGE - 1) & ~(ARENA_PAGE - 1);

    // Circle's memalign refuses 64 KB (alloc.cpp:33): align inside a larger block.
    uint8 *block = (uint8 *) malloc (s_nArenaSize + ARENA_PAGE);
    if (block == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No memory for a %u MB arena",
                                (unsigned) (s_nArenaSize >> 20));
        return false;
    }
    uint8 *arena = (uint8 *) (((uintptr) block + ARENA_PAGE - 1) & ~(uintptr) (ARENA_PAGE - 1));
    if (!ExecMemMakeExecutable (arena, s_nArenaSize))
    {
        CLogger::Get ()->Write (FROM, LogError, "The arena could not be made executable");
        return false;
    }

    s_pArena = arena;
    CLogger::Get ()->Write (FROM, LogNotice, "Arena: %u MB at %lx, executable",
                            (unsigned) (s_nArenaSize >> 20), (unsigned long) arena);
    return true;
}

void *vm_acquire (size_t size, int options)
{
    (void) options;

    if (!ArenaClaim ())
    {
        return VM_MAP_FAILED;
    }

    size = (size + ALLOC_ALIGN - 1) & ~(ALLOC_ALIGN - 1);
    if (size > s_nArenaSize - s_nUsed)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "Arena exhausted: %lu bytes asked, %lu of %lu used",
                                (unsigned long) size, (unsigned long) s_nUsed,
                                (unsigned long) s_nArenaSize);
        return VM_MAP_FAILED;
    }

    void *p = s_pArena + s_nUsed;
    s_nUsed += size;
    s_nLive++;
    if (s_nUsed > s_nHighWater)
    {
        s_nHighWater = s_nUsed;
    }
    return p;
}

// A bump allocator gives nothing back piece by piece. What it can do is start
// again once everything is released, which is what a restart of the Macintosh
// produces when the compiler shuts down in order.
int vm_release (void *addr, size_t size)
{
    (void) size;

    if (addr == VM_MAP_FAILED || addr == 0)
    {
        return 0;
    }
    if (s_nLive != 0 && --s_nLive == 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Arena empty again, %lu KB was the most",
                                (unsigned long) (s_nHighWater >> 10));
        s_nUsed = 0;
    }
    return 0;
}

// The whole arena is readable, writable and executable from the start, which
// is every protection the JIT ever asks for.
int vm_protect (void *addr, size_t size, int prot)
{
    (void) addr; (void) size; (void) prot;
    return 0;
}

/*
 *  Time
 */

uint64 GetTicks_usec (void)
{
    return CTimer::GetClockTicks64 ();
}

// tick_okapia.cpp
extern void TickFromEngine (void);

void jit_one_tick (void)
{
    TickFromEngine ();
}

// cpu_ticks_okapia.cpp — the seam: input, network, clock. The interpreter
// reaches it on an opcode count; compiled code retires no countable opcodes, so
// here it is the clock that decides, looked at once in 64 returns of the
// dispatcher and acted on once a millisecond.
extern void cpu_do_check_ticks (void);

// main_okapia.cpp: how often the guest asked for its instruction cache to be
// flushed. With a hard flush each request throws every translation away.
volatile unsigned g_nJitFlushRequests;

// What the arena has handed out, for the log line below.
static unsigned ArenaUsedMB (void)
{
    return (unsigned) (s_nUsed >> 20);
}
static unsigned ArenaHighMB (void)
{
    return (unsigned) (s_nHighWater >> 20);
}

void TimerPoll (void)
{
    static unsigned s_nUntilLook;
    static unsigned s_nLastVisit;
    static unsigned s_nLastReport;
    static unsigned s_nLastFlushes;

    if (s_nUntilLook-- != 0)
    {
        return;
    }
    s_nUntilLook = 63;

    const unsigned nNow = CTimer::GetClockTicks ();
    if (nNow - s_nLastVisit >= 1000)
    {
        s_nLastVisit = nNow;
        cpu_do_check_ticks ();

        if (nNow - s_nLastReport >= 5000000)
        {
            const unsigned nFlushes = g_nJitFlushRequests;
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "guest cache flushes: %u in %u s (%u in total), "
                                    "arena %u MB used, %u MB at most, of %u",
                                    nFlushes - s_nLastFlushes,
                                    (nNow - s_nLastReport) / 1000000, nFlushes,
                                    ArenaUsedMB (), ArenaHighMB (),
                                    (unsigned) (s_nArenaSize >> 20));
            s_nLastFlushes = nFlushes;
            s_nLastReport = nNow;
        }
    }
}

// The core sleeps a millisecond at a time while the 68k is STOPped (newcpu.cpp,
// SleepAndWait); the interpreter's old core had that line compiled out. Once the
// JIT has taken the tick from the timer interrupt (tick_inhibit), a STOPped
// Macintosh would wait for a tick nobody sends — so the wait is where it comes
// from, by the clock, as in the dispatch loop.
extern bool tick_inhibit;

int usleep (unsigned long)
{
    if (tick_inhibit)
    {
        static unsigned s_nLastTick;
        const unsigned nNow = CTimer::GetClockTicks ();
        if (nNow - s_nLastTick >= 16625)
        {
            // A stall must not unleash a burst, as in the dispatch loop.
            s_nLastTick = (nNow - s_nLastTick > 100000) ? nNow : s_nLastTick + 16625;
            jit_one_tick ();
        }
        cpu_do_check_ticks ();
    }
    return 0;
}

// Upstream hands the tick back to its thread when the JIT is off. Here the
// tick is an interrupt that tick_inhibit gates, and TickStart() clears it.
void TimerRestoreAsyncOwnership (void)
{
}

/*
 *  Preferences the JIT may not be left to choose
 */
void JitPrefsApply (void)
{
    // jitlazyflush is the preference's own, for now: false throws every
    // translation away at each cache flush the guest asks for, true keeps them
    // until a checksum says the code changed. The first is slow (the Pi 4, 7x),
    // the second can run stale code — the checksum was blind to the order of
    // instructions until macemu-jit's review of 2026-09-06 — which is old disk
    // writes. Which one Okapia ships is not decided: see
    // docs/notes/research/jit-68k.md, "The Pi 4 run".
    // No JIT FPU is built on AArch64: floating point stays in the binary128
    // core (patches/macemu-jit/0001).
    PrefsReplaceBool ("jitfpu", false);

#ifdef OKAPIA_JIT_CONSTJUMP
    setenv ("B2_JIT_ENABLE_CONSTJUMP", "1", 1);
#endif
#ifdef OKAPIA_JIT_DIAG
    // The core reads this from the environment, which a bare-metal kernel has not.
    setenv ("B2_JIT_DIAG", "1", 1);
#endif

    CLogger::Get ()->Write (FROM, LogNotice, "JIT %s, translation cache %d KB",
                            PrefsFindBool ("jit") ? "on" : "off (set \"jit true\")",
                            (int) PrefsFindInt32 ("jitcachesize"));
}

#endif
