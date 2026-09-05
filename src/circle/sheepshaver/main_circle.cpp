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

#include "okapia_circle.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "rom_patches.h"
#include "thunks.h"
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

// And the Macintosh's device space, which nothing here emulates. The ROM pokes
// the serial and power registers on its way up — main_unix.cpp lists the exact
// instructions it has to step over — and a port that cannot trap has to give
// those accesses somewhere to land. Reads come back as whatever was last
// written, which is what a register nobody drives would do.
uint8 gDevicePage[0x1000];

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

    s_nMacMemorySize = s_Layout.nHostBytes;

    // HEAP_ANY: above 1 GB when the board has it, low memory otherwise. A raw
    // block and not operator new — there are no constructors to run over a
    // quarter of a gigabyte.
    s_pMacMemory = (uint8 *) CMemorySystem::HeapAllocate (s_nMacMemorySize, HEAP_ANY);
    if (s_pMacMemory == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot allocate %u MB for the Mac",
                                (unsigned) (s_nMacMemorySize / (1024 * 1024)));
        return false;
    }

    // One offset, and everything else follows from it.
    VMBaseDiff = MacLayoutBaseDiff (&s_Layout, s_pMacMemory);

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
    memset (gDevicePage, 0, sizeof gDevicePage);

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
