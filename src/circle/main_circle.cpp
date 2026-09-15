/*
 * main_circle.cpp — Okapia platform layer: memory, ROM, interrupts, mutexes.
 *
 * Not a main(): the Circle kernel owns that. This provides the services the
 * Basilisk II core expects from its platform, and owns the Mac address space.
 *
 * Derived from BasiliskII/src/Unix/main_unix.cpp (Christian Bauer et al.),
 * reduced to what a bare-metal target needs. Changes from that file:
 *   - the Mac RAM block is claimed before any driver exists, not by mmap
 *   - mutexes map to Circle spinlocks; there are no pthreads here
 *   - alerts go to the serial log; there is no user to show a dialog to
 *   - no signal handling, no VOSF, no JIT
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include "okapia_circle.h"
#include "mac_ram_circle.h"
#include <stdio.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "rom_patches.h"
#include "xpram.h"
#include "timer.h"
#include "video.h"
#include "emul_op.h"

#define FROM "okapia"

/*
 *  Mac address space
 *
 *  Basilisk wants one contiguous block holding RAM, then the ROM, then a small
 *  scratch area (main_unix.cpp:731). Under DIRECT_ADDRESSING a Mac address is a
 *  host address minus MEMBaseDiff, so the block's base defines the whole map.
 */

const uint32 ROM_MAX_SIZE     = 1024 * 1024;
const uint32 SCRATCH_MEM_SIZE = 64 * 1024;

/*
 * RAMBaseHost, RAMSize, ROMBaseHost, ROMSize, RAMBaseMac, ROMBaseMac and
 * MEMBaseDiff all belong to uae_cpu_2021/basilisk_glue.cpp — defining them here
 * too is a duplicate-symbol error at link time. We only fill them in.
 */
uint8 *ScratchMem;               // Scratch memory for Mac ROM writes

// Emulator state the core reads directly
int  CPUType;
bool CPUIs68060;
int  FPUType;
bool TwentyFourBitAddressing;
uint32 InterruptFlags;

static uint8 *s_pMacMemory;      // the one allocation, freed never
static size_t s_nMacMemorySize;
static CSpinLock s_IntFlagsLock;

/*
 *  Claim the Mac address space.
 *
 *  Must run before any driver initialises. Circle serves blocks larger than its
 *  largest bucket by walking forward through free space, so a 257 MB request
 *  made after the USB or network stacks have allocated will fail on a 1 GB board
 *  even though the memory exists.
 */

#if DIRECT_ADDRESSING_GUARD
/*
 *  Guest addresses outside the block (patches/macemu/0007, OKAPIA_TRACE only)
 *
 *  Called from the interpreter's translation step, so it may not block, may not
 *  allocate and may not log: a runaway would fill the serial port faster than
 *  the port can empty it, and the first address is the one that matters anyway.
 *  It counts, remembers the first, and lets the access proceed — the fault that
 *  follows is then attributable.
 */

uae_u32 direct_addressing_limit;

static uae_u32 s_nFirstStray;
static unsigned s_nStrayCount;

// The Mac's own frame buffer lives on the heap past the block, so the first
// stray is nearly always a pixel. Anything far beyond it — the I/O space of the
// machine being emulated, most of all — is counted apart, because that is the
// access that lands on the Pi's memory or on its peripherals.
static const uae_u32 FAR_BEYOND = 0x40000000;
static uae_u32 s_nFirstFar;
static uae_u32 s_nLastFar;
static unsigned s_nFarCount;

void direct_addressing_fault (uaecptr addr)
{
    if (s_nStrayCount++ == 0)
    {
        s_nFirstStray = addr;
    }
    if (addr >= FAR_BEYOND)
    {
        if (s_nFarCount++ == 0)
        {
            s_nFirstFar = addr;
        }
        s_nLastFar = addr;
    }
}

void GuestBoundsReport (void)
{
    static unsigned s_nReported;
    if (s_nStrayCount != s_nReported)
    {
        s_nReported = s_nStrayCount;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "guest address out of the block: %u so far, "
                                "first at %08lx (limit %08lx)",
                                s_nStrayCount,
                                (unsigned long) s_nFirstStray,
                                (unsigned long) direct_addressing_limit);
    }
    static unsigned s_nFarReported;
    if (s_nFarCount != s_nFarReported)
    {
        s_nFarReported = s_nFarCount;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "guest address far beyond the block: %u so far, "
                                "first at %08lx, last at %08lx",
                                s_nFarCount, (unsigned long) s_nFirstFar,
                                (unsigned long) s_nLastFar);
    }
}
#endif

bool MacMemoryAllocate (uint32 nRAMSize)
{
    RAMSize = nRAMSize;
    // Claimed at the size the *other* engine would need as well, so whichever
    // Macintosh runs first the block is taken once: the image carries both and
    // a second quarter-gigabyte is not there to be had (mac_ram_circle.h).
    s_nMacMemorySize = RAMSize + OKAPIA_MAC_BLOCK_OVERHEAD;

    s_pMacMemory = (uint8 *) MacRamClaim (s_nMacMemorySize);
    if (s_pMacMemory == 0)
    {
        return false;
    }

    RAMBaseHost = s_pMacMemory;
    ROMBaseHost = RAMBaseHost + RAMSize;
#if DIRECT_ADDRESSING_GUARD
    // Everything the Macintosh may legitimately touch is inside this block, so
    // its size is the whole of the rule. Set here rather than in a trace file
    // of its own because this is where the number is known, and because an
    // address is only unguarded until this line runs — which is before the
    // processor exists.
    direct_addressing_limit = s_nMacMemorySize;
#endif
    ScratchMem  = ROMBaseHost + ROM_MAX_SIZE + SCRATCH_MEM_SIZE / 2;

#if DIRECT_ADDRESSING
    // Mac address = host address - MEMBaseDiff, and Mac RAM starts at zero.
    MEMBaseDiff = (uintptr) RAMBaseHost;
    RAMBaseMac = 0;
    ROMBaseMac = Host2MacAddr (ROMBaseHost);
#else
    #error "Okapia builds with DIRECT_ADDRESSING"
#endif

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac memory: %u MB RAM at %p, ROM at %p, Mac base diff 0x%lX",
                            (unsigned) (RAMSize / (1024 * 1024)),
                            RAMBaseHost, ROMBaseHost, (unsigned long) MEMBaseDiff);
    return true;
}

/*
 *  Load and validate the ROM.
 *
 *  CheckROM() insists on a 32-bit clean image (rom_patches.cpp:838); refusing
 *  early with a clear reason beats failing somewhere inside the 68k boot.
 */

bool MacROMLoad (const char *pFileName)
{
    FILE *pFile = fopen (pFileName, "rb");
    if (pFile == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "ROM not found: %s", pFileName);
        return false;
    }

    fseek (pFile, 0, SEEK_END);
    long nSize = ftell (pFile);
    fseek (pFile, 0, SEEK_SET);

    if (nSize != 512 * 1024 && nSize != 1024 * 1024)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "ROM is %ld bytes; expected 512 KB or 1 MB", nSize);
        fclose (pFile);
        return false;
    }

    ROMSize = (uint32) nSize;
    if (fread (ROMBaseHost, 1, ROMSize, pFile) != ROMSize)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot read %s", pFileName);
        fclose (pFile);
        return false;
    }
    fclose (pFile);

    if (!CheckROM ())
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "ROM is not 32-bit clean (version word 0x%04X): "
                                "DIRECT_ADDRESSING requires one that is",
                                (unsigned) ((ROMBaseHost[8] << 8) | ROMBaseHost[9]));
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "ROM: %s, %u KB, 32-bit clean",
                            pFileName, (unsigned) (ROMSize / 1024));
    return true;
}

/*
 *  Interrupt flags
 *
 *  Read by the 68k loop on one core and written from timer and device handlers
 *  on core 0, so the update needs a lock rather than EnterCritical.
 */

void SetInterruptFlag (uint32 flag)
{
    s_IntFlagsLock.Acquire ();
    InterruptFlags |= flag;
    s_IntFlagsLock.Release ();
}

void ClearInterruptFlag (uint32 flag)
{
    s_IntFlagsLock.Acquire ();
    InterruptFlags &= ~flag;
    s_IntFlagsLock.Release ();
}

/*
 *  Mutexes. Circle's scheduler is cooperative, so a spinlock is both correct
 *  and the cheapest thing that works across cores.
 */

struct B2_mutex
{
    CSpinLock Lock;
};

B2_mutex *B2_create_mutex (void)
{
    return new B2_mutex;
}

void B2_lock_mutex (B2_mutex *mutex)
{
    if (mutex != 0)
    {
        mutex->Lock.Acquire ();
    }
}

void B2_unlock_mutex (B2_mutex *mutex)
{
    if (mutex != 0)
    {
        mutex->Lock.Release ();
    }
}

void B2_delete_mutex (B2_mutex *mutex)
{
    delete mutex;
}

/*
 *  Alerts. There is no dialog to raise: the serial log is the only place a
 *  message can go, and on real hardware the screen belongs to the Mac.
 */

void ErrorAlert (const char *text)
{
    CLogger::Get ()->Write (FROM, LogError, "%s", text);
}

void WarningAlert (const char *text)
{
    CLogger::Get ()->Write (FROM, LogWarning, "%s", text);
}

bool ChoiceAlert (const char *text, const char *pos, const char *neg)
{
    // Nobody to ask. Report and take the negative answer, which is the safe one.
    CLogger::Get ()->Write (FROM, LogWarning, "%s -- answering '%s'", text, neg);
    return false;
}

/*
 * The core calls this on unrecoverable errors and on "Shut Down". There is
 * nowhere to return to, so record the reason and let the kernel decide between
 * halting and rebooting — the clean-shutdown path of the plan hangs off here.
 */

static bool s_bQuitRequested = false;

// newcpu.h:281. Leaving the interpreter takes two steps, and doing only one is
// a silent hang: quit_program alone is tested by the OUTER loop (newcpu.cpp
// :1562), while m68k_do_execute() spins in an inner for(;;) that only SPCFLAG_BRK
// interrupts. m68k_emulop_return() sets both, which is why we call it rather than
// poking the flag ourselves. Upstream then does exit(0); we have nowhere to exit
// to, so we unwind through Start680x0() and let the kernel tear down in order.
extern void m68k_emulop_return (void);

void QuitEmulator (void)
{
    CLogger::Get ()->Write (FROM, LogNotice, "Emulator quit requested");
    s_bQuitRequested = true;

    // Without this the Mac powers off, the 68k loop keeps running past the
    // PowerOff() trap, and the disk image is never closed — which is exactly
    // how the HFS volume ends up dirty (AGENTS.md).
    m68k_emulop_return ();
}

bool QuitRequested (void)
{
    return s_bQuitRequested;
}

/*
 *  Instruction cache. Without a JIT nothing generates code at run time, but the
 *  68k can still ask for a flush; honouring it costs nothing and keeps the
 *  behaviour right if a JIT ever lands.
 */

void FlushCodeCache (void *start, uint32 size)
{
    InvalidateInstructionCache ();
}

/*
 *  Idle handling. The Mac calls these when it has nothing to do. Yielding lets
 *  the other cores breathe; Circle has no preemption to rely on.
 *
 *  The first call is worth more than the yield. It is the Macintosh saying it
 *  has finished starting — a fact no proxy metric recovers, and one this
 *  project has twice tried to guess: a high opcode rate and a non-empty guest
 *  buffer are equally true of the question-mark floppy. screenshot.sh and
 *  run-test.sh read the line below instead of counting seconds.
 *
 *  It only arrives if patch_idle_time() found its pattern, which is what
 *  HasIdleTime() answers; a System with no SynchIdleTime never idles and the
 *  line never comes, so the scripts fall back on their timeout.
 */

static bool     s_bIdle = false;
static bool     s_bIdleSeen = false;
static unsigned s_nIdleCalls = 0;

void idle_wait (void)
{
    s_bIdle = true;

    if (!s_bIdleSeen)
    {
        s_bIdleSeen = true;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Macintosh idle: it has finished starting (%u ms)",
                                CTimer::Get ()->GetClockTicks () / 1000);
    }
    s_nIdleCalls++;

    // The one point at which the Macintosh is known to be between jobs rather
    // than inside one, which is what anything re-entering the Toolbox needs.
    // Does its work once and returns immediately ever after.
    extern void TellMacTheMouseResolution (void);
    TellMacTheMouseResolution ();
#if SOUND_SELFTEST
    extern void AudioSelfTest (void);
    AudioSelfTest ();
#endif

    CTimer::SimpleusDelay (100);
}

void idle_resume (void)
{
    s_bIdle = false;
}

/*
 *  Whether the Macintosh has ever been idle, for anything that wants to know
 *  the boot is over without watching the screen.
 */
bool MacHasBeenIdle (void)
{
    return s_bIdleSeen;
}
