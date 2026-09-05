//
// kernel_ppc.cpp — Okapia: bring up the board, then hand it to a PowerMacintosh.
//
// The SheepShaver kernel, and deliberately the smallest one that can reach a
// screen. It does what kernel.cpp does for the other engine, minus everything
// phase 20 does not need to answer its one question: does the Macintosh start?
// No shared folder, no clock refinement from the card, no volume repair, no
// restart loop. Those are not hard, they are simply not the question, and each
// of them is a place a first boot could fail for a reason that has nothing to
// do with the engine.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel_ppc.h"
#include "okapia_input.h"

#include <stdio.h>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "thunks.h"

#define FROM "okapia-ppc"

// src/circle/sheepshaver/main_circle.cpp
extern bool MacMemoryAllocate (uint32 nRAMSize);
extern bool MacROMLoad (const char *pFileName);

// kpx_cpu/sheepshaver_glue.cpp — the PowerPC emulator itself. There is no
// Start680x0() on this engine: the Macintosh is entered by jumping into the
// ROM's nanokernel at a fixed offset, which is what main_unix.cpp:1387 does.
extern void init_emul_ppc (void);
extern void emul_ppc (uint32 nStart);
extern void exit_emul_ppc (void);

// src/circle/tick_circle.cpp
extern void TickInit (void);
extern void TickStart (void);
extern void TickStop (void);

// src/circle/input_circle.cpp
extern void InputInit (void);

// A PowerMacintosh wants more than a Quadra before it will start at all.
static const uint32 MIN_MAC_RAM = 16 * 1024 * 1024;

CKernelPPC::CKernelPPC (void)
{
}

CKernelPPC::~CKernelPPC (void)
{
}

bool CKernelPPC::Initialize (void)
{
    if (!m_Board.Start (FROM))
    {
        return false;
    }
    m_Board.StartConsole ();

    if (!m_Board.StartCard ())
    {
        CLogger::Get ()->Write (FROM, LogError, "No SD card, or it will not mount");
        return false;
    }

    // The preferences say how much Mac RAM to allocate, so they come first —
    // the one thing allowed before the block.
    int argc = 0;
    char **argv = 0;
    PrefsInit (0, argc, argv);

    uint32 nRAMSize = (uint32) PrefsFindInt32 ("ramsize");
    if (nRAMSize < MIN_MAC_RAM)
    {
        CLogger::Get ()->Write (FROM, LogError, "ramsize %u is below the %u MB floor",
                                (unsigned) nRAMSize,
                                (unsigned) (MIN_MAC_RAM / (1024 * 1024)));
        return false;
    }

    // Before any driver allocates: Circle serves large blocks by walking
    // forward through free space.
    if (!MacMemoryAllocate (nRAMSize))
    {
        return false;
    }

    if (!m_Board.StartUSB ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No USB: keyboard and mouse unavailable");
    }
    FwInputWatch ();

    return true;
}

TShutdownMode CKernelPPC::Run (void)
{
    // Before the ROM and before InitAll, in that order, because ThunksInit()
    // inside InitAll allocates out of this area — and with base left at zero it
    // allocates out of Low Memory instead, silently. Upstream does it at
    // main_unix.cpp:1142, three lines before loading the ROM.
    if (!SheepMem::Init ())
    {
        CLogger::Get ()->Write (FROM, LogError, "SheepMem would not initialise");
        return ShutdownHalt;
    }

    const char *pROM = PrefsFindString ("rom");
    if (pROM == 0 || !MacROMLoad (pROM))
    {
        return ShutdownHalt;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Initialising the emulator");
    if (!InitAll (0))
    {
        CLogger::Get ()->Write (FROM, LogError, "InitAll failed");
        return ShutdownHalt;
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac RAM at %p (guest 0x%08X), ROM at %p (guest 0x%08X)",
                            RAMBaseHost, (unsigned) RAMBase,
                            ROMBaseHost, (unsigned) ROMBase);

    InputInit ();
    TickInit ();
    TickStart ();

    CLogger::Get ()->Write (FROM, LogNotice, "Entering PowerPC execution");
    init_emul_ppc ();
    emul_ppc (ROMBase + 0x310000);  // does not return until the Macintosh stops
    TickStop ();
    exit_emul_ppc ();

    CLogger::Get ()->Write (FROM, LogNotice, "PowerPC execution ended");
    ExitAll ();
    return ShutdownHalt;
}
