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
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include "okapia_circle.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "ether.h"
#include "sigsegv.h"

#define FROM "okapia-ppc"

/*
 *  The 60 Hz tick, held off across a mode switch
 *
 *  emul_op.cpp sets it while the Macintosh changes execution mode and clears it
 *  after. tick_circle.cpp does not read it yet — the tick simply keeps running —
 *  which is a thing to come back to if the first boot behaves oddly around a
 *  mode switch.
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
 *  Ethernet
 *
 *  The dummy driver has no reset because it has nothing to reset. The call
 *  comes from the Macintosh changing execution mode (emul_op.cpp:293), which
 *  happens whether or not there is a network.
 */
void ether_reset (void)
{
}

/*
 *  Code the Macintosh has just written
 *
 *  On a real PowerMac this flushes the caches. Under an interpreter there are
 *  no caches to flush — but there is a decode cache, and kpx_cpu invalidates it
 *  through FlushCodeCache(), which the CPU glue provides. So this is genuinely
 *  nothing to do here rather than something not done yet.
 */
void MakeExecutable (int dummy, uint32 start, uint32 length)
{
    (void) dummy; (void) start; (void) length;
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
