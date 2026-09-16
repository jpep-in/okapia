/*
 * emul_op_hook_circle.cpp — listen in on the Mac's emulator operations.
 *
 * Two of Basilisk's own opcodes say something Okapia needs to hear, and both
 * arrive through EmulOp(). Hooked at link time rather than by patching
 * external/ (docs/contributing/build.md). Circle invokes ld directly, so the flag is a bare
 * --wrap.
 *
 * M68K_EMUL_OP_CLKNOMEM is the one path by which XPRAM changes while the guest
 * runs (emul_op.cpp:159 and :170), so there is an event to listen to, and
 * polling the 256 bytes on a timer would be both later and dumber: later
 * because a change waits for the next tick, dumber because the comparison runs
 * when nothing has happened.
 *
 * M68K_EMUL_OP_RESET sits at ROMBase+0x8c (rom_patches.cpp:1069), on the 68000's
 * reset path, and its handler rebuilds the boot globals and loads the registers
 * for the boot routine (emul_op.cpp:87). So it is the Macintosh starting — and
 * it runs again on every restart from Mac OS, which is the only notice we get
 * that the guest is going round. Without it the firmware's window is
 * unreachable after the first boot: the Mac reloads its System and nobody was
 * ever asked.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "emul_op.h"

#include "okapia_circle.h"

static const char FROM[] = "okapia-restart";

extern void XPRAMWatchdog (void);

// newcpu.h:281 — leaving the interpreter takes two steps, and doing only one is
// a silent hang. The same exit QuitEmulator() uses; see main_circle.cpp.
extern void m68k_emulop_return (void);

// newcpu.cpp:66. The interpreter's outer loop breaks on it and leaves it set
// (newcpu.cpp:1562), because upstream exits the process next and never has to
// start again. Okapia does, and a second Start680x0() would return at once —
// which reads as "the Macintosh would not start" and is nothing of the kind.
extern int quit_program;

static unsigned s_nResets  = 0;
static bool     s_bRestart = false;

/*
 *  Called by the kernel before each run of the interpreter.
 */
void MacRestartArm (void)
{
    s_nResets  = 0;
    s_bRestart = false;
    quit_program = 0;
}

bool MacRestarted (void)
{
    return s_bRestart;
}

extern "C" void __real__Z6EmulOptP13M68kRegisters (uint16 opcode, M68kRegisters *r);

extern "C" void __wrap__Z6EmulOptP13M68kRegisters (uint16 opcode, M68kRegisters *r)
{
    __real__Z6EmulOptP13M68kRegisters (opcode, r);

    // One comparison per EmulOp, against a constant. Everything else — the VBL,
    // the disk driver, every trap Basilisk patches — passes through untouched.
    if (opcode == M68K_EMUL_OP_CLKNOMEM)
    {
        XPRAMWatchdog ();
    }
    else if (opcode == M68K_EMUL_OP_RESET)
    {
        // The first one belongs to this run of the emulator: Start680x0() enters
        // the ROM at its reset vector, so a cold start is indistinguishable from
        // a restart and there is nothing to go back to yet. Measured on System
        // 7.1 and on 7.6.1, headless and with a window: an ordinary boot to the
        // Finder produces exactly one. Anything after it is Mac OS going round.
        if (++s_nResets > 1 && !s_bRestart)
        {
            // The safest instant in the whole session to stop: Mac OS restarts
            // through the Shut Down Manager, so it has already unmounted its
            // volumes and marked them clean. Every other moment would leave the
            // volume in use, exactly as a pulled plug does.
            CLogger::Get ()->Write (FROM, LogNotice, "The Macintosh restarted");
            s_bRestart = true;
            m68k_emulop_return ();
        }
    }
}
