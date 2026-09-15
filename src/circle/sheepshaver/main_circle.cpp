/*
 * main_circle.cpp — SheepShaver's Macintosh, placed in a Raspberry Pi's memory.
 *
 * The counterpart of src/circle/main_circle.cpp for the other engine, and of
 * SheepShaver/src/Unix/main_unix.cpp upstream. What it does not contain is as
 * deliberate as what it does: the interrupt and CPU plumbing lives with the CPU,
 * and arrives when there is one to plumb. Everything here is memory, the ROM,
 * and the handful of platform answers the core asks for on the way.
 *
 * The layout itself is in mac_layout.cpp, checked on a development machine.
 * This file allocates what that plan describes and says where it landed.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <stdio.h>
#include <string.h>

// okapia_circle.h first, always: it is what defines ASSERT_STATIC, and every
// Circle header fails to parse without it (see that file).
#include "okapia_circle.h"
#include "mac_ram_circle.h"
#include "okapia_output.h"
#include "hal_circle.h"

// kpx_cpu/sheepshaver_glue.cpp
extern void exit_emul_ppc (void);
extern void XPRAMExit (void);
extern void DiskExit (void);
#include <circle/startup.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "rom_patches.h"
#include "thunks.h"
#include "xlowmem.h"
#include "mac_layout.h"

#define FROM "okapia-ppc"

// Our constants and upstream's must be the same constants. They are repeated in
// mac_layout.h so that the arithmetic can be checked without a macemu tree; this
// is where the two are made to agree, at compile time and once.
static_assert (OKAPIA_ROM_SIZE == ROM_SIZE, "ROM size");
static_assert (OKAPIA_ROM_AREA_SIZE == ROM_AREA_SIZE, "ROM area size");
static_assert (OKAPIA_KERNEL_DATA_BASE == KERNEL_DATA_BASE, "kernel data base");

/*
 *  What the core reads
 */

uint32 RAMBase;
uint32 RAMSize;
uint8 *RAMBaseHost;
uint32 ROMBase;
uint8 *ROMBaseHost;
uint32 KernelDataAddr;
uint32 BootGlobsAddr;
uint32 DRCacheAddr;

// A 7400 with AltiVec, which is what main_unix.cpp assumes when it cannot ask
// the host, and what the ROM is happiest being told.
uint32 PVR = 0x000c0000;
int64  CPUClockSpeed = 100000000;
int64  BusClockSpeed = 100000000;
int64  TimebaseSpeed =  25000000;

volatile uint32 InterruptFlags = 0;

// The offset that turns a guest address into a host one. A variable and not a
// constant, which is what patches/macemu/0001 is for.
uintptr VMBaseDiff;

// The two regions kept out of the block, because the ROM fixes their addresses
// 1.6 GB apart and covering that with one offset would mean reserving 1.6 GB to
// use 20 KB of it. vm.hpp routes accesses here instead; see the same patch.
uint8 gZeroPage[0x3000];
uint8 gKernelData[0x2000];

// And everything outside the block: the Macintosh's device space, which nothing
// here emulates, and the strays that a host with signals absorbs through
// "ignoresegv" — true by default in both emulators, so this is upstream's
// behaviour rather than a shortcut. Reads come back as whatever was last
// written there, which is what a register nobody drives would do.
//
// Counted, and reported, because absorbing quietly is the part that would be a
// shortcut. The bounds are the block's, set once it is allocated; before that
// gGuestSize is zero and every address is a stray, which is correct — there is
// nowhere for one to be. The first address reached is kept as well: a count
// says how often the Macintosh went astray, and only the first one says where.
uint32 gGuestLow, gGuestSize, gStrayCount, gStrayFirst;
uint8  gStrayPage[0x1000];

// SheepShaver's 32-bit addressable scratch, whose statics upstream defines in
// its own main_*.cpp.
uint32  SheepMem::page_size;
uintptr SheepMem::zero_page = 0;
uintptr SheepMem::base = 0;
uintptr SheepMem::proc;
uintptr SheepMem::data;

static uint8 *s_pMacMemory;       // the one allocation, freed never
static size_t s_nMacMemorySize;
static TMacLayout s_Layout;

/*
 *  Claim the Mac address space
 *
 *  Before any driver initialises, for the same reason as the other engine:
 *  Circle serves blocks larger than its largest bucket by walking forward
 *  through free space, so a quarter-gigabyte request made after the USB or
 *  network stacks have allocated fails on a 1 GB board even though the memory
 *  is there.
 */

bool MacMemoryAllocate (uint32 nRAMSize)
{
    if (!MacLayoutPlan (nRAMSize, &s_Layout))
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "%u MB of Mac RAM cannot be placed",
                                (unsigned) (nRAMSize / (1024 * 1024)));
        return false;
    }

    // The layout says what this engine needs; the claim asks for what *either*
    // engine needs, so the block is taken once for the image (mac_ram_circle.h).
    // The surplus sits past nEnd, where nothing is mapped and nothing looks.
    s_nMacMemorySize = nRAMSize + OKAPIA_MAC_BLOCK_OVERHEAD;
    if (s_nMacMemorySize < s_Layout.nHostBytes)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "The shared overhead is %u KB and this layout wants %u KB",
                                (unsigned) (OKAPIA_MAC_BLOCK_OVERHEAD / 1024),
                                (unsigned) ((s_Layout.nHostBytes - nRAMSize) / 1024));
        return false;
    }

    s_pMacMemory = (uint8 *) MacRamClaim (s_nMacMemorySize);
    if (s_pMacMemory == 0)
    {
        return false;
    }

    // One offset, and everything else follows from it — including the bounds,
    // which must be set in the same breath. Mac2HostAddr() consults them, so
    // between VMBaseDiff and these every guest address is outside the block and
    // translates to the stray page. Setting them a few lines later put
    // ROMBaseHost inside a 4 KB array and DecodeROM wrote 4 MB into it.
    VMBaseDiff = MacLayoutBaseDiff (&s_Layout, s_pMacMemory);
    gGuestLow  = s_Layout.nRAMBase;
    gGuestSize = s_Layout.nEnd - s_Layout.nRAMBase;

    RAMBase     = s_Layout.nRAMBase;
    RAMSize     = s_Layout.nRAMSize;
    RAMBaseHost = Mac2HostAddr (RAMBase);
    ROMBase     = s_Layout.nROMBase;
    ROMBaseHost = Mac2HostAddr (ROMBase);

    // Fixed by the ROM, reached through gKernelData, and still announced to the
    // core by its guest address: the ROM boot structure is patched with it
    // (rom_patches.cpp:751).
    KernelDataAddr = KERNEL_DATA_BASE;

    // Low Memory is banked too, and starts cleared. The Mac writes its own
    // globals there; what it never wrote it must not read as leftovers.
    memset (gZeroPage, 0, sizeof gZeroPage);
    memset (gKernelData, 0, sizeof gKernelData);
    memset (gStrayPage, 0, sizeof gStrayPage);

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac memory: %u MB at %p, guest 0x%08X, ROM guest 0x%08X, "
                            "offset 0x%lX",
                            (unsigned) (RAMSize / (1024 * 1024)), s_pMacMemory,
                            (unsigned) RAMBase, (unsigned) ROMBase,
                            (unsigned long) VMBaseDiff);
    return true;
}

/*
 *  SheepShaver's own scratch, inside the block rather than beside it
 *
 *  Upstream asks the host for another fixed address here. The plan already put
 *  the room aside, so this is arithmetic: the area, and a read-only page of
 *  zeros in the middle of it that also catches the two halves growing into each
 *  other (thunks.h).
 */

bool SheepMem::Init (void)
{
    page_size = 0x1000;

    base = s_Layout.nSheepBase;
    proc = base;
    data = base + size;

    zero_page = proc + (size / 2);
    Mac_memset (zero_page, 0, page_size);
    return true;
}

void SheepMem::Exit (void)
{
    // Nothing to give back: the block is claimed for the life of the board.
}

// Where the Macintosh's screen lives, for video_circle.cpp. A Mac address,
// because screen_base is one; zero before the block is allocated.
uint32 MacFrameBufferGuest (void)
{
    return s_pMacMemory != 0 ? s_Layout.nFrameBase : 0;
}

uintptr SignalStackBase (void)
{
    return s_Layout.nSigStack + OKAPIA_SIG_STACK_SIZE;
}

/*
 *  Load the ROM
 *
 *  DecodeROM() takes both shapes SheepShaver accepts — a plain 4 MB image and a
 *  <CHRP-BOOT> file, which it decompresses — so a "Mac OS ROM" taken from a
 *  System Folder is used as it stands (rom_patches.cpp:148-199).
 *  scripts/check-rom.py judges the same file by the same rules before a card is
 *  ever booted, and says which of the six nanokernels it carries.
 */

bool MacROMLoad (const char *pFileName)
{
    FILE *pFile = fopen (pFileName, "rb");
    if (pFile == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "ROM not found: %s", pFileName);
        return false;
    }

    // Upstream reads at most ROM_SIZE, which covers a compressed file whole and
    // a plain image exactly.
    uint8 *pTemp = new uint8[ROM_SIZE];
    if (pTemp == 0)
    {
        fclose (pFile);
        return false;
    }
    const size_t nRead = fread (pTemp, 1, ROM_SIZE, pFile);
    fclose (pFile);

    const bool bOK = DecodeROM (pTemp, (uint32) nRead);
    delete[] pTemp;

    if (!bOK)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "%s: %u bytes, and neither a 4 MB image nor a "
                                "<CHRP-BOOT> file", pFileName, (unsigned) nRead);
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "ROM: %s, %u bytes read, decoded to 4 MB",
                            pFileName, (unsigned) nRead);
    return true;
}

/*
 *  Interrupts
 *
 *  TriggerInterrupt() is not here: the CPU glue owns it, because raising an
 *  interrupt means poking the emulated processor. These four are the flags it
 *  reads, and the nesting count the ROM keeps in its own low memory.
 *
 *  A spin lock rather than the atomics upstream uses: the flags are set from
 *  the tick handler, which runs at IRQ level, and read from the 68k thread.
 */

static CSpinLock s_IntFlagsLock;

void SetInterruptFlag (uint32 nFlag)
{
    s_IntFlagsLock.Acquire ();
    InterruptFlags |= nFlag;
    s_IntFlagsLock.Release ();
}

void ClearInterruptFlag (uint32 nFlag)
{
    s_IntFlagsLock.Acquire ();
    InterruptFlags &= ~nFlag;
    s_IntFlagsLock.Release ();
}

// The nesting count lives in the Mac's own low memory, where the nanokernel
// reads it (xlowmem.h). Signed on purpose: the ROM takes it below zero.
void DisableInterrupt (void)
{
    WriteMacInt32 (XLM_IRQ_NEST, int32 (ReadMacInt32 (XLM_IRQ_NEST)) + 1);
}

void EnableInterrupt (void)
{
    WriteMacInt32 (XLM_IRQ_NEST, int32 (ReadMacInt32 (XLM_IRQ_NEST)) - 1);
}

/*
 *  What the core says, and where it goes
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
    // Nobody to ask, and the core only uses this to offer a way round a failure.
    // Taking the negative answer means a refusal stays a refusal.
    CLogger::Get ()->Write (FROM, LogWarning, "%s — answering \"%s\"", text, neg);
    return false;
}

/*
 *  Stopping
 *
 *  Upstream unwinds an emulator thread here. There is no thread and no shell to
 *  return to: the board has one job. So the Macintosh stopping stops the
 *  machine, and the log says why — which is the whole of what a person watching
 *  a serial port needs. Phase 22 will make this go round again instead.
 */

// Set by ether_reset() when the Macintosh resets itself a second time — see
// platform_bits_circle.cpp. It turns the stop below into a board reset, which
// is the only way back to the firmware's window on this engine. Not static, on
// purpose: it is read from the other file, and a name beginning with s_ would
// have said the opposite.
bool g_bMacRestartWanted;

void QuitEmulator (void)
{
    CLogger::Get ()->Write (FROM, LogNotice, "The Macintosh asked to stop");

    // Not halt() straight away: ExitAll() is the only thing that closes the disk
    // image, and closing it is what flushes the last of it to the card.
    // Skipping it left, on a Shut Down — the one path where the Macintosh did
    // everything right — exactly what pulling the plug leaves.
    //
    // The order is upstream's (Quit(), main_unix.cpp:1220) and the order is the
    // whole of it. Leaving exit_emul_ppc() out, to avoid freeing an interpreter
    // we are standing inside, hung the shutdown instead: the drivers close
    // through the Macintosh, and the Macintosh is that interpreter. Upstream
    // reaches here from the same place — PowerOff() is patched to EMUL_RETURN
    // (rom_patches.cpp:2251), which lands in QuitEmulator() — so its order is
    // the tested one and there is no reason to invent another.
    extern void TickStop (void);
    TickStop ();
    exit_emul_ppc ();

    // Two calls, and not ExitAll(). ExitAll() hangs here, measured: it closes
    // thirteen drivers and several of them go through the Macintosh, which is
    // the interpreter exit_emul_ppc() has just taken down. Upstream survives
    // the same order because it exits the process next and nothing has to work
    // afterwards; here the board has to reach halt() or reboot().
    //
    // These two are the ones data safety is about — the PRAM as the Mac left
    // it, and the disk image closed, which is what flushes the last of it to
    // the card — and they are proven to complete from this context. The rest is
    // tidying for a program that is about to stop existing.
    XPRAMExit ();
    DiskExit ();

    // Leave a black screen rather than the Macintosh's last frame. The board is
    // about to reset, and between the reset and Circle claiming the display
    // again the GPU keeps scanning out this buffer — but the new frame buffer
    // is not the old one's size, so the same bytes are read back with another
    // pitch and another depth. What that shows is the last desktop, magnified
    // and in the wrong colours, for about a second. Nothing is wrong with it
    // and it looks exactly like something is.
    CBcmFrameBuffer *pOutput = FwOutputClaim ();
    if (pOutput != 0)
    {
        memset ((void *) (uintptr) pOutput->GetBuffer (), 0,
                (size_t) pOutput->GetPitch () * pOutput->GetHeight ());
    }

    if (g_bMacRestartWanted)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Restarted from Mac OS: disk closed, resetting the board");
        BoardLogFlush ();
        reboot ();
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Shut down cleanly, disk closed");
    BoardPowerOff ();
    halt ();
}

/*
 *  Mutexes
 *
 *  Circle's scheduler is cooperative and the emulator runs on one core, so a
 *  lock is a counter that must never be contended. Being told when it is beats
 *  a lock that quietly does nothing.
 */

struct B2_mutex
{
    int nHeld;
};

B2_mutex *B2_create_mutex (void)
{
    B2_mutex *pMutex = new B2_mutex;
    if (pMutex != 0)
    {
        pMutex->nHeld = 0;
    }
    return pMutex;
}

void B2_lock_mutex (B2_mutex *pMutex)
{
    if (pMutex != 0 && pMutex->nHeld++ != 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Mutex taken twice");
    }
}

void B2_unlock_mutex (B2_mutex *pMutex)
{
    if (pMutex != 0)
    {
        pMutex->nHeld--;
    }
}

void B2_delete_mutex (B2_mutex *pMutex)
{
    delete pMutex;
}
