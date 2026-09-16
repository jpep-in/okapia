/*
 * platform_bits_circle.cpp — the last handful SheepShaver asks a host for.
 *
 * Every one of these is small, and every one of them lives in main_unix.cpp
 * upstream, mixed in with a thousand lines of signal handling that has no
 * meaning here. Kept apart rather than copied into main_circle.cpp so that what
 * is genuinely ours — memory, the ROM, interrupts — stays legible beside what is
 * merely owed.
 *
 * Nothing here is silent. A stub that matters says so when it is called; a stub
 * that cannot matter says why in a comment instead of at run time.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include "okapia_circle.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "ether.h"
#include "sigsegv.h"

#include <string.h>

#include "okapia_firmware.h"
#include "okapia_input.h"
#include "hal_circle.h"

#define FROM "okapia-ppc"

/*
 *  The 60 Hz tick, held off across a mode switch
 *
 *  emul_op.cpp sets it around the Macintosh's early reset (OP_RESET) and clears
 *  it after; tick_circle.cpp's periodic handler returns at once while it is set,
 *  the same test upstream's tick thread makes (main_unix.cpp:1529).
 */
bool tick_inhibit;

/*
 *  Time
 *
 *  kpx_cpu reads the timebase through this. Circle counts microseconds since
 *  the board started, which is exactly what is wanted: a monotonic count, not a
 *  date.
 */
// Counted, because it is the cheapest proof that the guest is executing at all:
// the nanokernel reads the timebase constantly, and a count that never moves
// means the interpreter is not running rather than running and stuck.
volatile unsigned g_nTimebaseReads;

uint64 GetTicks_usec (void)
{
    g_nTimebaseReads++;
    return CTimer::GetClockTicks64 () / (CLOCKHZ / 1000000);
}

/*
 *  Idling
 *
 *  Upstream lets the emulator thread sleep when the Macintosh has nothing to
 *  do, and wakes it on an interrupt. There is one thread here and it is the
 *  Macintosh, so sleeping would stop the machine rather than free it. Doing
 *  nothing is the correct implementation, not a placeholder.
 */
void idle_wait (void)
{
}

void idle_resume (void)
{
}

/*
 *  Preferences, the engine's own hooks
 *
 *  prefs.h declares these for SheepShaver only. Everything they would do —
 *  finding the file, applying defaults — is already done by prefs_circle.cpp,
 *  which both engines share.
 */
void prefs_init (void)
{
}

void prefs_exit (void)
{
}

/*
 *  Ethernet, and the only notice this engine gets that the Macintosh restarted
 *
 *  The dummy driver has nothing to reset. What makes this worth keeping is
 *  where it is called from: OP_RESET, "early in MacOS reset" (emul_op.cpp:286),
 *  which is the one place a PowerPC Macintosh announces that it is going round
 *  again. The other engine has Basilisk's own reset opcode for this; here there
 *  is no equivalent, and without it a Restart from the Finder resets the
 *  nanokernel *inside* the emulator and reloads the same System — which is what
 *  it did, with no way back to the boot menu and the disk image never closed.
 *
 *  An ordinary boot produces exactly one reset, so the first is the cold start
 *  and every later one is the guest restarting — the same reading
 *  docs/topics/startup-and-shutdown.md records for the 68k engine, measured there rather than assumed here.
 *
 *  What happens then is the firmware's window, as on the other engine, but in
 *  place: the Macintosh is halfway through its own reset and simply waits
 *  inside this call. Answering "start the same volume" lets that reset carry
 *  on, which is exactly what a desktop SheepShaver does on a Restart and takes
 *  no time at all. It used to leave through QuitEmulator() and reset the board
 *  every time: seven seconds of the Pi's own firmware for a restart the other
 *  Macintosh does instantly.
 *
 *  Anything else still leaves that way, because this engine cannot be started
 *  a second time in place — SheepShaver exits the process upstream and pairs no
 *  InitAll with its ExitAll: another volume, the other Macintosh, another sound
 *  output or a settings change reset the board, and Shut Down halts it. QuitEmulator() is the one
 *  path that stops the tick, unwinds the interpreter and closes the disk.
 *
 *  Nothing the Macintosh had open is at risk meanwhile: it unmounted its
 *  volumes before resetting, and the disk layer holds no cache (docs/topics/storage.md).
 */

// input_circle.cpp and video_circle.cpp, this engine's copies.
extern void InputRelease (void);
extern void InputInit (void);
extern void VideoReclaim (void);

// The startup as the preferences state it: everything the firmware's answer
// can change that this engine could not take in place.
struct TStartup
{
    char  Disk[256];
    char  Cdrom[256];
    char  Sound[16];        // the device is chosen as the engine starts
    bool  bNoSound;
    int32 nBootDriver;
};

static void StartupRead (TStartup *pOut)
{
    const char *pDisk  = PrefsFindString ("disk", 0);
    const char *pCdrom = PrefsFindString ("cdrom", 0);
    strncpy (pOut->Disk,  pDisk  != 0 ? pDisk  : "", sizeof pOut->Disk - 1);
    strncpy (pOut->Cdrom, pCdrom != 0 ? pCdrom : "", sizeof pOut->Cdrom - 1);
    pOut->Disk[sizeof pOut->Disk - 1] = '\0';
    pOut->Cdrom[sizeof pOut->Cdrom - 1] = '\0';
    pOut->nBootDriver = PrefsFindInt32 ("bootdriver");
    const char *pSound = PrefsFindString ("soundoutput");
    strncpy (pOut->Sound, pSound != 0 ? pSound : "", sizeof pOut->Sound - 1);
    pOut->Sound[sizeof pOut->Sound - 1] = '\0';
    pOut->bNoSound = PrefsFindBool ("nosound");
}

void ether_reset (void)
{
    static unsigned s_nResets;

    if (++s_nResets <= 1)
    {
        return;
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "The Macintosh restarted itself (reset #%u)", s_nResets);

    TStartup Before, After;
    StartupRead (&Before);

    InputRelease ();
    FwInputReclaim ();
    BoardChimePlay ();          // a Macintosh chimes when it restarts
    const TFirmwareResult Result = FirmwareRun (FirmwareEnginePowerPC);
    StartupRead (&After);
    // The Macintosh is about to feed the same device, and nothing on this path
    // calls AudioInit(), which would otherwise wait for the chime.
    BoardChimeFinish ();

    if (   Result == FirmwareBoot
        && FirmwareWantedEngine () == FirmwareEnginePowerPC
        && strcmp (Before.Disk, After.Disk) == 0
        && strcmp (Before.Cdrom, After.Cdrom) == 0
        && Before.nBootDriver == After.nBootDriver
        && strcmp (Before.Sound, After.Sound) == 0
        && Before.bNoSound == After.bNoSound)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Same startup: restarting in place");
        InputInit ();
        VideoReclaim ();
        return;
    }

    extern bool g_bMacRestartWanted;
    g_bMacRestartWanted = Result != FirmwareHalt;
    CLogger::Get ()->Write (FROM, LogNotice, "%s",
                            Result == FirmwareHalt ? "Shut down from the firmware"
                                                   : "Another startup: resetting the board");
    QuitEmulator ();
}

/*
 *  Code the Macintosh has just written
 *
 *  On a real PowerMac this flushes the processor's caches. Under an interpreter
 *  there is no instruction cache — but there is a decode cache, keyed by
 *  address, and it is exactly as stale as an unflushed icache would be. Leaving
 *  this empty cost a boot: the Macintosh idles fine on the question-mark floppy,
 *  because it loads no code, and comes apart a dozen seconds into starting a
 *  System from disk, because that is nothing but loading code into addresses
 *  something else has already been decoded at. What ran then was the previous
 *  occupant of those bytes.
 *
 *  Upstream does the same thing under EMULATED_PPC (main_unix.cpp:1473),
 *  ROM excepted: the ROM is written once, before the first instruction is
 *  decoded, and PatchROM's own writes would otherwise flush 4 MB a time.
 */
void MakeExecutable (int dummy, uint32 start, uint32 length)
{
    (void) dummy;
    if (start >= ROMBase && start < ROMBase + ROM_SIZE)
    {
        return;
    }
    FlushCodeCache (start, start + length);
}

/*
 *  Diagnostics the core offers and this port does not take
 */

void Dump68kRegs (M68kRegisters *r)
{
    CLogger::Get ()->Write (FROM, LogNotice,
                            "68k D0 %08x D1 %08x A0 %08x A1 %08x",
                            (unsigned) r->d[0], (unsigned) r->d[1],
                            (unsigned) r->a[0], (unsigned) r->a[1]);
}

// PatchAfterStartup() is main.cpp's own, not the platform's.

/*
 *  The fault handler that cannot fire
 *
 *  sheepshaver_glue.cpp carries a SIGSEGV handler, and this port has no signals
 *  and — deliberately — nothing that faults: the Macintosh's device space is
 *  routed to memory (patches/macemu/0001). These exist so that handler links,
 *  and installing one says plainly that nothing will ever call it, rather than
 *  leaving somebody to wonder later.
 */

bool sigsegv_install_handler (sigsegv_fault_handler_t handler)
{
    (void) handler;
    CLogger::Get ()->Write (FROM, LogNotice,
                            "No fault handler installed: device accesses are "
                            "routed to memory, so none can arrive");
    return true;
}

sigsegv_address_t sigsegv_get_fault_address (sigsegv_info_t *sip)
{
    (void) sip;
    return 0;
}

sigsegv_address_t sigsegv_get_fault_instruction_address (sigsegv_info_t *sip)
{
    (void) sip;
    return 0;
}

/*
 *  The disassembler
 *
 *  kpx_cpu prints instructions when something has already gone wrong, through
 *  binutils' interface. Bringing that in for a path taken only after a crash is
 *  a poor trade; these keep it linkable and say nothing.
 */

extern "C"
{
    int print_insn_ppc (unsigned long long addr, void *info)
    {
        (void) addr; (void) info;
        return 4;                       // one PowerPC instruction, unprinted
    }
    void generic_print_address (unsigned long long addr, void *info)
    {
        (void) addr; (void) info;
    }
    int generic_symbol_at_address (unsigned long long addr, void *info)
    {
        (void) addr; (void) info;
        return 0;
    }
    void perror_memory (int status, unsigned long long addr, void *info)
    {
        (void) status; (void) addr; (void) info;
    }
}
