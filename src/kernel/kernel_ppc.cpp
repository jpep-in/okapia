//
// kernel_ppc.cpp — Okapia: bring up the board, then hand it to a PowerMacintosh.
//
// The SheepShaver kernel, and deliberately smaller than the other one. What the
// card is owed before any Macintosh opens it — the inventory, the clock floor,
// the repair and the name of the startup volume — it does exactly as kernel.cpp
// does, through card_circle.cpp: a card is not less at risk for starting the
// other engine. No shared folder preparation and no restart loop yet.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel_ppc.h"
#include "okapia_input.h"
#include "okapia_firmware.h"
#include "card_circle.h"

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
    if (!OkapiaBoard ().Start (FROM))
    {
        return false;
    }
    OkapiaBoard ().StartConsole ();

    if (!OkapiaBoard ().StartCard ())
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

    if (!OkapiaBoard ().StartUSB ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No USB: keyboard and mouse unavailable");
    }
    FwInputWatch ();

    return true;
}

TOkapiaExit CKernelPPC::Run (bool bSwitched)
{
    // The firmware has its say first, exactly as on the other engine: the card
    // is mounted, the preferences are read and USB is up. It is also what reads
    // the card's answer to "which emulator", and this image carries one — so a
    // startup volume asking for the other one is handed over to it.
    // Skipped on the pass that follows a hand-over: see CKernel::Run.
    const TFirmwareResult Result = bSwitched ? FirmwareBoot
                                             : FirmwareRun (FirmwareEnginePowerPC);
    switch (Result)
    {
    case FirmwareHalt:
        CLogger::Get ()->Write (FROM, LogNotice, "Powered off from the firmware");
        return OkapiaHalt;

    case FirmwareReboot:
        return OkapiaReboot;

    case FirmwareBoot:
    default:
        break;
    }

    if (FirmwareWantedEngine () != FirmwareEnginePowerPC)
    {
        return OkapiaSwitchTo68k;
    }

    // Before the ROM and before InitAll, in that order, because ThunksInit()
    // inside InitAll allocates out of this area — and with base left at zero it
    // allocates out of Low Memory instead, silently. Upstream does it at
    // main_unix.cpp:1142, three lines before loading the ROM.
    if (!SheepMem::Init ())
    {
        CLogger::Get ()->Write (FROM, LogError, "SheepMem would not initialise");
        return OkapiaHalt;
    }

    // This engine's own ROM, falling back to the other's keyword: a card holds
    // both Macintoshes now, and they do not take the same ROM — SheepShaver
    // runs a real PowerMac one where Basilisk replaces the Toolbox.
    const char *pROM = PrefsFindString ("romppc");
    if (pROM == 0 || pROM[0] == '\0')
    {
        pROM = PrefsFindString ("rom");
    }
    if (pROM == 0 || !MacROMLoad (pROM))
    {
        return OkapiaHalt;
    }

    // What kernel.cpp does before its own InitAll, in the same order and by the
    // same code: the inventory, the clock raised from it before anything can
    // stamp a date, then the repair — which also names the volume this session
    // is about, and run-test.sh judges the volume that line names.
    CardInventory ();
    CardRefineClock ();
    if (!CardPrepareVolumes ())
    {
        return OkapiaHalt;
    }
    if (CardRepairOnly ())
    {
        return OkapiaHalt;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Initialising the emulator");
    if (!InitAll (0))
    {
        CLogger::Get ()->Write (FROM, LogError, "InitAll failed");
        return OkapiaHalt;
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac RAM at %p (guest 0x%08X), ROM at %p (guest 0x%08X)",
                            RAMBaseHost, (unsigned) RAMBase,
                            ROMBaseHost, (unsigned) ROMBase);

    InputInit ();
    TickInit ();
    TickStart ();

    CLogger::Get ()->Write (FROM, LogNotice, "Entering PowerPC execution");
    BoardLogFlush ();       // everything up to here, before anything can crash
    init_emul_ppc ();
    emul_ppc (ROMBase + 0x310000);  // does not return until the Macintosh stops
    TickStop ();
    exit_emul_ppc ();

    CLogger::Get ()->Write (FROM, LogNotice, "PowerPC execution ended");
    ExitAll ();

    // Back to the firmware, by way of the board rather than by going round.
    // Reaching here is not a shut down — that goes through QuitEmulator(),
    // which closes the drivers and halts without ever returning — so it is a
    // restart or a stop nobody named, and both want the menu.
    //
    // The other engine loops instead: Basilisk's reset opcode unwinds the
    // interpreter and InitAll() can be paired with ExitAll(). This one cannot
    // yet — SheepShaver has no restart path upstream, it exits the process — so
    // a second InitAll() over a torn-down nanokernel is untested and would fail
    // in a way nobody could read. A reset costs a second and brings the window
    // back, which is what a person restarting wants; halting left them with no
    // way to change System at all.
    return OkapiaReboot;
}
