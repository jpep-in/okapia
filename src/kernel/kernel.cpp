//
// kernel.cpp — Okapia: bring up the board, then hand it to the Macintosh.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include "okapia_firmware.h"
#include "okapia_input.h"
#include <circle_glue.h>
#include <circle/memory.h>
#include <stdio.h>
#include <sys/stat.h>

// Basilisk II headers. sysdeps.h must come first, as everywhere in that tree.
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "xpram.h"
#include "spcflags.h"
#include "hfs_volume_circle.h"
#include "card_circle.h"

#define FROM "okapia"

// From src/circle/main_circle.cpp
extern bool MacMemoryAllocate (uint32 nRAMSize);
extern bool MacROMLoad (const char *pFileName);
extern bool QuitRequested (void);

// emul_op_hook_circle.cpp — the Macintosh going round again.
extern void MacRestartArm (void);
extern bool MacRestarted (void);

// input_circle.cpp — let go of the devices so the next InputInit() takes them.
extern void InputRelease (void);

// From src/circle/tick_circle.cpp
extern void TickInit (void);
extern void TickStart (void);
extern void TickStop (void);

// From src/circle/input_circle.cpp
extern void InputInit (void);

#ifdef FPU_SELFTEST
// fpu_ieee.cpp (patches/macemu/0008). Declared here rather than in a header:
// it exists only in a traced build and has exactly one caller.
extern "C" int fpu_selftest_extended (uint32 *first);
#endif

// Everything that used to be a constant here now comes from the preferences
// file on the card (see src/circle/prefs_circle.cpp and README.md). What is
// left is the floor below which a Mac cannot start at all, so that a typo in
// "ramsize" produces a message instead of a failed allocation.
static const uint32 MIN_MAC_RAM = 4 * 1024 * 1024;

CKernel::CKernel (void)
{
}

CKernel::~CKernel (void)
{
}

bool CKernel::Initialize (void)
{
    // 1. The board: serial, the log, interrupts, the timer and its clock. Every
    //    reason this order matters is in hal_circle.cpp, once.
    if (!OkapiaBoard ().Start (FROM))
    {
        return false;
    }

    // 2. The console, on the serial port. Before any file is opened — see
    //    COkapiaBoard::StartConsole.
    OkapiaBoard ().StartConsole ();

    // 3. The card, then the preferences on it. They decide how much Mac RAM to
    //    allocate, so they have to be read before the allocation — which is the
    //    one thing allowed to come before it.
    if (!OkapiaBoard ().StartCard ())
    {
        CLogger::Get ()->Write (FROM, LogError, "No SD card, or it will not mount");
        return false;
    }

    LoadPreferences ();
    ApplyTimeZone ();

    // 4. The Mac memory block, before any other driver allocates: Circle serves
    //    large blocks by walking forward through free space. The card driver is
    //    ahead of it now, so log what it cost rather than assuming it cost
    //    nothing.
    CLogger::Get ()->Write (FROM, LogNotice, "Free before Mac RAM: %u MB low, %u MB high",
                    (unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_LOW) / (1024*1024)),
                    (unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_ANY) / (1024*1024)));

    uint32 nRAMSize = (uint32) PrefsFindInt32 ("ramsize");
    if (nRAMSize < MIN_MAC_RAM)
    {
        CLogger::Get ()->Write (FROM, LogError, "ramsize %u is below the %u MB floor",
                        (unsigned) nRAMSize, (unsigned) (MIN_MAC_RAM / (1024*1024)));
        return false;
    }
    if (!MacMemoryAllocate (nRAMSize))
    {
        return false;
    }

    // The chime, as early as it can know which ROM to take it from: it plays
    // from a timer while USB and the boot menu come up. Not again when the
    // other engine hands back: that is the same start, not a restart.
    BoardChimePlay ();

    // 5. Everything else.
    if (!OkapiaBoard ().StartUSB ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No USB: keyboard and mouse unavailable");
    }

    // The firmware starts listening here rather than when its window opens. A
    // USB keyboard reports changes and not state, so Option held from power-on
    // sends its one report before the window and nothing during it — which made
    // the combination work when a script sent it and never when a person held
    // it. There is no state to read back: CUSBKeyboardDevice will say when
    // something changes and never what is down.
    FwInputWatch ();

    return true;
}

/*
 *  Preferences
 *
 *  Nothing is decided here any more: PrefsInit() applies the defaults from
 *  src/circle/prefs_circle.cpp and then lets the card's BasiliskII_Prefs
 *  override them, in upstream's own format with upstream's own parser.
 */

void CKernel::LoadPreferences (void)
{
    // PrefsInit wants argc/argv by reference; there is no command line here.
    int argc = 0;
    char **argv = 0;
    PrefsInit (0, argc, argv);

    CLogger::Get ()->Write (FROM, LogNotice,
                    "%u MB RAM, model %d, CPU 680%d0, FPU %s, sound %s, frameskip %d",
                    (unsigned) (PrefsFindInt32 ("ramsize") / (1024 * 1024)),
                    (int) PrefsFindInt32 ("modelid"),
                    (int) PrefsFindInt32 ("cpu"),
                    PrefsFindBool ("fpu") ? "yes" : "no",
                    PrefsFindBool ("nosound") ? "off" : "on",
                    (int) PrefsFindInt32 ("frameskip"));
}

/*
 *  Which hour to show
 *
 *  The build time is a UTC instant and Circle's clock reports UTC by default,
 *  but a Mac of this era has no notion of a time zone at all: its clock is
 *  local time and that is the end of it. So a Pi in Paris shows an hour or two
 *  behind, which looks like a stale build and is not.
 *
 *  The offset has to be applied before the time, because SetTime stores local
 *  seconds — setting the zone afterwards would move the log's idea of the hour
 *  and leave the Mac's untouched.
 */

void CKernel::ApplyTimeZone (void)
{
    int nMinutes = (int) PrefsFindInt32 ("timezone");
    if (nMinutes != 0)
    {
        if (   !CTimer::Get ()->SetTimeZone (nMinutes)
            || !CTimer::Get ()->SetTime (OKAPIA_BUILD_TIME, FALSE))
        {
            CLogger::Get ()->Write (FROM, LogWarning, "timezone %d is out of range, staying on UTC",
                            nMinutes);
            nMinutes = 0;
            // Back to UTC in the timer too, whichever of the two calls refused:
            // CardRefineClock() reads the zone from the timer, and an offset
            // accepted there and then abandoned here would put the two frames
            // it compares an hour or two apart.
            CTimer::Get ()->SetTimeZone (0);
        }
    }

    // Circle's formatter has no %+d, so the sign is spelled out.
    const unsigned nAbs = (unsigned) (nMinutes < 0 ? -nMinutes : nMinutes);
    CLogger::Get ()->Write (FROM, LogNotice,
                    "Clock: %s (UTC%s%u:%02u), from the build time — no RTC and no NTP yet",
                    (const char *) *CTimer::Get ()->GetTimeString (),
                    nMinutes < 0 ? "-" : "+", nAbs / 60, nAbs % 60);
}

/*
 *  Match the model id to the System that is actually installed
 *
 *  The lesson of 2026-08-29: a System 7.1 carrying its enabler checks the
 *  machine it runs on and stops on an empty Welcome box when it is told it is a
 *  Quadra 900. The model id follows the System, not the ROM, and two Systems of
 *  different families cannot share one setting — which is why guessing it in a
 *  preferences file is the wrong place for it when the volume can be asked.
 *
 *  Mac OS 8 dropped the Mac IIci, so it needs a Quadra; everything before 7.5
 *  needs the IIci. 7.5 and 7.6 run on either, and 5 is what they are tested on
 *  here, so the boundary is drawn at 8.0 and nowhere else.
 */

void CKernel::ApplyModelId (void)
{
    if (!PrefsFindBool ("modelidauto"))
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Model %d, from the preferences (modelidauto false)",
                        (int) PrefsFindInt32 ("modelid"));
        return;
    }
    const char *pBootVolume = CardBootVolume ();
    if (pBootVolume[0] == '\0')
    {
        return;
    }

    THfsSystemVersion Version;
    if (!HfsSystemVersion (pBootVolume, &Version))
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                        "%s: no System version found, keeping model %d",
                        pBootVolume, (int) PrefsFindInt32 ("modelid"));
        return;
    }

    const int32 nModelID = Version.nMajor >= 8 ? 14 : 5;
    const int32 nWas = PrefsFindInt32 ("modelid");
    PrefsReplaceInt32 ("modelid", nModelID);

    // Which emulator could take this volume, said out loud while the version is
    // in hand. Nothing acts on it yet — the boot menu will — but a card that
    // will one day be offered a choice should already say what it is.
    static const char *const FlavourName[] =
        { "unreadable", "68k", "PowerPC", "universal" };

    CLogger::Get ()->Write (FROM, LogNotice,
                    "%s: \"%s\" says System %u.%u.%u (%s), %s — model %d%s",
                    pBootVolume, Version.File,
                    Version.nMajor, Version.nMinor, Version.nBugfix,
                    Version.Short[0] != '\0' ? Version.Short : "no short version",
                    FlavourName[HfsFlavourOf (&Version)],
                    (int) nModelID,
                    nModelID == nWas ? "" : ", overriding the preferences");
}

/*
 *  The shared folder
 *
 *  Basilisk's ExtFS shows a host directory to the Mac as a volume. Here that
 *  directory is on the card, next to the disk images, so files reach the Mac by
 *  being copied onto the card from anything that reads FAT.
 *
 *  It needs the File System Manager 1.2: nothing to install from System 7.5 on,
 *  a system extension for 7.0 and 7.1 (verified on 7.1.2 with Apple's FSM 1.2
 *  SDK). Without it extfs.cpp says so — "No FSM present, disabling ExtFS" —
 *  rather than failing quietly. The FSM is not File Sharing: that is AppleShare
 *  over the network, and it does not provide this.
 */

void CKernel::PrepareSharedFolder (void)
{
    const char *pPath = PrefsFindString ("extfs");
    if (pPath == 0 || *pPath == '\0')
    {
        CLogger::Get ()->Write (FROM, LogNotice, "No shared folder (extfs unset)");
        return;
    }

    // ExtFSInit gives up silently on a path that is not a directory, so create
    // it rather than leave the Mac without a volume and no reason given.
    struct stat Stat;
    if (stat (pPath, &Stat) != 0)
    {
        if (mkdir (pPath, 0777) != 0)
        {
            CLogger::Get ()->Write (FROM, LogWarning, "Shared folder %s: cannot create it", pPath);
            return;
        }
        CLogger::Get ()->Write (FROM, LogNotice, "Shared folder %s: created", pPath);
        return;
    }
    if (!S_ISDIR (Stat.st_mode))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Shared folder %s: not a directory", pPath);
        return;
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                    "Shared folder %s as \"%s\" (System 7.0 and 7.1 need the "
                    "File System Manager 1.2 extension)",
                    pPath, PrefsFindString ("extfsname"));
}

/*
 *  Let the card correct the keyboard
 *
 *  The built-in table is generated from Basilisk's own keycodes file, so it is
 *  right for the keyboards Basilisk knows about. A card can still override it —
 *  a keyboard that reports an unusual usage is fixed by two lines in a file
 *  rather than by a rebuild, which is the difference between an appliance and a
 *  development machine.
 *
 *  "keycodefile" is upstream's keyword for exactly this, so it keeps that name.
 */

void CKernel::LoadKeycodes (void)
{
    extern bool KeycodesLoad (const char *pPath);

    const char *pPath = PrefsFindString ("keycodefile");
    if (pPath == 0 || *pPath == '\0')
    {
        return;
    }
    if (!KeycodesLoad (pPath))
    {
        // Absent is the normal case, not a fault: the built-in table stands.
        CLogger::Get ()->Write (FROM, LogNotice, "No keyboard override at %s", pPath);
    }
}

/*
 *  Hand over to the Macintosh.
 */

bool CKernel::StartMacintosh (void)
{
    const char *pROM = PrefsFindString ("rom");
    if (pROM == 0 || !MacROMLoad (pROM))
    {
        return false;
    }

    CardInventory ();
    CardRefineClock ();
    PrepareSharedFolder ();
    LoadKeycodes ();

    if (!CardPrepareVolumes ())
    {
        return false;
    }

    // Before InitAll: rom_patches.cpp reads modelid while patching the ROM.
    ApplyModelId ();

    // Before InitAll, after the repair: see card_circle.h.
    if (CardRepairOnly ())
    {
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Initialising the emulator");
#ifdef FPU_SELFTEST
    // Does a 68881 extended value survive a trip through an fpu_register?
    // The FPU's one property decidable without a Macintosh, and the one that
    // fails when the register is a double: eleven bits of mantissa go missing
    // and anything past 1e308 is clamped. Off unless OKAPIA_TRACE=1, because it
    // is a fact about the build and not about this boot.
    {
        uint32 First[6] = { 0 };
        const int nBad = fpu_selftest_extended (First);
        if (nBad == 0)
        {
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "FPU: every extended pattern came back unchanged");
        }
        else
        {
            CLogger::Get ()->Write (FROM, LogWarning,
                                    "FPU: %d extended pattern(s) lost; "
                                    "%08x %08x %08x came back %08x %08x %08x",
                                    nBad, First[0], First[1], First[2],
                                    First[3], First[4], First[5]);
        }
    }
#endif

    if (!InitAll (0))
    {
        CLogger::Get ()->Write (FROM, LogError, "InitAll failed");
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Mac RAM at %p (Mac 0x%08X), ROM at %p (Mac 0x%08X)",
                    RAMBaseHost, (unsigned) RAMBaseMac,
                    ROMBaseHost, (unsigned) ROMBaseMac);
    CLogger::Get ()->Write (FROM, LogNotice, "CPU type %d, FPU %d, 24-bit addressing %s",
                    CPUType, FPUType, TwentyFourBitAddressing ? "on" : "off");

    // USB devices are attached once, before the 68k loop takes over. Circle's
    // plug-and-play needs polling from core 0, which is not available while
    // Start680x0() runs; hot-plug will come with the multicore split (§7.3).
    InputInit ();

    // The heartbeat, armed only now: everything it pokes must already exist.
    TickInit ();
    TickStart ();

    CLogger::Get ()->Write (FROM, LogNotice, "Entering 68k execution");
    BoardLogFlush ();       // everything up to here, before anything can crash
    MacRestartArm ();
    Start680x0 ();
    TickStop ();                          // does not return until the Mac stops

    CLogger::Get ()->Write (FROM, LogNotice, "68k execution ended");
    return true;
}

TOkapiaExit CKernel::Run (bool bSwitched)
{
    // The Macintosh may go round more than once. A restart from Mac OS is a
    // reset of the 68000, and Basilisk's own opcode sits on that path
    // (emul_op_hook_circle.cpp), so the emulator can be unwound and set up
    // again — which puts the firmware's window in front of every restart,
    // exactly where it is on a cold boot. That is the whole point: without it
    // the menu is reachable once per power-on and never again.
    //
    // Rebooting the Pi would arrive at the same screen and cost more. It re-runs
    // the whole of Circle, and it loses the very key the window is waiting for:
    // USB re-enumerates and a keyboard reports changes, never state, so Option
    // held across the reboot would arrive nowhere. Under QEMU it ends the
    // session outright.
    //
    // There is deliberately no cut-out for a Macintosh that restarts on its own.
    // One was tried and it was worse than the thing it guarded: a System 7.1
    // boots in about three seconds, so somebody restarting a few times in a row
    // is indistinguishable from a loop, and the cut-out took the window away
    // from the person using it. And a loop is not a brick — the firmware's two
    // seconds come round every time, so Option still reaches the chooser and
    // still picks another volume. The log below names the pattern instead.

    for (;;)
    {
        // The firmware has its say before the emulator exists. Everything it
        // needs is already in place: the card is mounted, the preferences are
        // read, the Mac's memory is allocated and USB is up — Initialize() sees
        // to all four, and InputInit() only takes the keyboard for the Macintosh
        // later on.
        // Skipped only on the pass that follows a hand-over: the other engine's
        // window is the one that answered, and the firmware is shared, so it
        // still holds that answer. Cleared straight away, so a restart from
        // Mac OS opens the window as it always did.
        const TFirmwareResult Result = bSwitched ? FirmwareBoot
                                                 : FirmwareRun (FirmwareEngine68k);
        bSwitched = false;

        switch (Result)
        {
        case FirmwareHalt:
            CLogger::Get ()->Write (FROM, LogNotice, "Powered off from the firmware");
            return OkapiaHalt;

        case FirmwareReboot:
            CLogger::Get ()->Write (FROM, LogNotice, "Restarting Okapia at the firmware's request");
            return OkapiaReboot;

        case FirmwareBoot:
        default:
            break;
        }

        // The startup volume may ask for the other emulator, and this image
        // carries it. Saying so and returning is the whole of the hand-over:
        // okapia_boot.cpp calls the other engine, the board stays up, and
        // nothing is read off the card or reset. It used to be a chain boot —
        // a second kernel image, four megabytes read back, and a reboot — and
        // that is the cost the merged image removes.
        if (FirmwareWantedEngine () != FirmwareEngine68k)
        {
            return OkapiaSwitchToPowerPC;
        }

        const unsigned nStarted = CTimer::Get ()->GetTicks ();

        if (!StartMacintosh ())
        {
            CLogger::Get ()->Write (FROM, LogError, "The Macintosh did not start");
            // Stay alive rather than reboot: the serial log is the only diagnosis.
            for (unsigned i = 0; i < 10; i++)
            {
                CTimer::Get ()->MsDelay (1000);
            }
            return OkapiaHalt;
        }

        // ExitAll() closes the drivers in order — DiskExit() is what finally
        // calls Sys_close() on the disk image, flushing FatFs to the card. It
        // saves the PRAM itself, so no separate XPRAMExit() here.
        ExitAll ();

        // Not in ExitAll(), which leaves 680x0 emulation up: upstream exits the
        // process next and never has to pair it. Going round again does, and an
        // unpaired init_m68k() would set the FPU up a second time.
        Exit680x0 ();

        // Init680x0() takes a fresh one every time and nothing ever gives it
        // back (basilisk_glue.cpp:73). It is a few bytes, but an appliance is
        // meant to run for months and a few bytes per restart is still a leak.
        B2_delete_mutex (spcflags_lock);
        spcflags_lock = 0;

        if (QuitRequested ())
        {
            CLogger::Get ()->Write (FROM, LogNotice, "Shut down cleanly, disk closed");
            return OkapiaHalt;
        }

        if (!MacRestarted ())
        {
            // The interpreter left for a reason nobody named. Say so rather
            // than looping on it.
            CLogger::Get ()->Write (FROM, LogNotice, "68k execution ended, disk closed");
            return OkapiaReboot;
        }

        // The Macintosh had them; the firmware needs them back, and needs the
        // keys typed at the Macintosh not to count as an answer to a window
        // that had not opened yet.
        InputRelease ();
        FwInputReclaim ();

        // A Macintosh chimes when it restarts, not only when it is switched on.
        BoardChimePlay ();

        // How long it ran, and how much memory is left. Going round is where a
        // leak turns into a machine that stops booting after a while, and a
        // number that does not move is the cheapest proof that nothing is being
        // left behind. The seconds say whether the Macintosh was used or came
        // straight back, which is what a runaway restart would look like.
        CLogger::Get ()->Write (FROM, LogNotice,
                        "Restarted after %u s: disk closed, %u KB free,"
                        " the firmware has its say again",
                        (CTimer::Get ()->GetTicks () - nStarted) / HZ,
                        (unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_ANY) / 1024));
    }
}
