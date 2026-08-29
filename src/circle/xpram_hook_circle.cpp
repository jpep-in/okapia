/*
 * xpram_hook_circle.cpp — notice when the Mac changes its PRAM.
 *
 * The Mac reaches its clock/PRAM chip through one emulator operation,
 * M68K_EMUL_OP_CLKNOMEM, and that is the only path by which XPRAM changes while
 * the guest runs (emul_op.cpp:159 and :170). So there is an event to listen to,
 * and polling the 256 bytes on a timer would be both later and dumber: later
 * because a change waits for the next tick, dumber because the comparison runs
 * when nothing has happened.
 *
 * Hooked at link time rather than by patching external/ (AGENTS.md). Circle
 * invokes ld directly, so the flag is a bare --wrap.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "emul_op.h"

extern void XPRAMWatchdog (void);

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
}
