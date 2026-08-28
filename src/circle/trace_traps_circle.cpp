/*
 * trace_traps_circle.cpp — see the Toolbox calls the Mac makes.
 *
 * Every A-line instruction reaches op_illg(), which forwards it to the ROM's
 * trap dispatcher via Exception(0xA). Neither is reachable by --wrap directly:
 * the opcode table calls op_illg_1(), which calls op_illg() inside the same
 * file. op_illg_1() is the one that crosses a translation unit — it is
 * referenced from cpufunctbl.o — so that is where the hook goes. Its argument is the opcode, which for an A-line trap *is* the trap
 * word — so this is the cheapest full view of what the guest asks the Toolbox.
 *
 * Written to find out what the Mac examines before refusing to start from a
 * volume marked in use: the answer should show up as the last few file-system
 * traps before it gives up.
 *
 * Off unless src/kernel/Makefile passes OKAPIA_TRACE=1.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "cpu_emulation.h"
#include "macos_util.h"
#include "readcpu.h"
#include "newcpu.h"

extern "C" void __real__Z9op_illg_1j (unsigned int opcode);

// The File Manager traps worth naming; everything else is counted, not printed.
static const char *TrapName (unsigned int nTrap)
{
    switch (nTrap & 0x0FFF)
    {
    case 0x000F: return "MountVol";
    case 0x000E: return "UnmountVol";
    case 0x0013: return "FlushVol";
    case 0x0017: return "Eject";
    case 0x0060: return "FSDispatch";
    case 0x0001: return "Close";
    case 0x0002: return "Read";
    case 0x0003: return "Write";
    case 0x0004: return "Control";
    case 0x0005: return "Status";
    case 0x0007: return "GetVolInfo";
    case 0x0008: return "Create";
    case 0x0009: return "Delete";
    case 0x000A: return "OpenRF";
    case 0x0014: return "GetVol";
    case 0x0015: return "SetVol";
    case 0x001C: return "GetEOF";
    default:     return 0;
    }
}

static unsigned s_nTraps;
static unsigned s_nNamed;

// A File Manager call leaves its result in the parameter block: ioResult holds
// 1 while the call is in progress and the OSErr once it finishes. So remember
// the block that MountVol was handed, then watch it until it settles.
static uint32 s_nPendingPB;
static unsigned s_nPendingTrap;

// macos_util.h stops short of the volume errors, which are exactly the ones
// worth naming here.
enum
{
    volOnLinErr = -55,          // drive volume already on-line
    noMacDskErr = -57,          // not a Macintosh disk
    badMDBErr   = -60,          // bad master directory block
    wrPermErr   = -61           // read/write permission denied
};

static const char *ErrName (int16 nErr)
{
    switch (nErr)
    {
    case 0:      return "noErr";
    case ioErr:  return "ioErr";
    case nsvErr: return "nsvErr (no such volume)";
    case paramErr: return "paramErr";
    case wPrErr: return "wPrErr (write protected)";
    case permErr: return "permErr";
    case nsDrvErr: return "nsDrvErr";
    case extFSErr: return "extFSErr (external file system)";
    case noDriveErr: return "noDriveErr";
    case offLinErr: return "offLinErr";
    case badMDBErr: return "badMDBErr (bad master directory block)";
    case volOnLinErr: return "volOnLinErr (already mounted)";
    case noMacDskErr: return "noMacDskErr (not a Mac disk)";
    case wrPermErr: return "wrPermErr (permission denied)";
    case memFullErr: return "memFullErr";
    default:     return "?";
    }
}

extern "C" void __wrap__Z9op_illg_1j (unsigned int opcode)
{
    if ((opcode & 0xF000) == 0xA000)
    {
        s_nTraps++;

        // Has the call we are watching finished?
        if (s_nPendingPB != 0)
        {
            int16 nResult = (int16) ReadMacInt16 (s_nPendingPB + ioResult);
            if (nResult != 1)
            {
                CLogger::Get ()->Write ("okapia-trap", LogNotice,
                                        "  -> MountVol (trap #%u) returned %d  %s",
                                        s_nPendingTrap, (int) nResult, ErrName (nResult));
                s_nPendingPB = 0;
            }
        }

        if ((opcode & 0x0FFF) == 0x000F)        // MountVol
        {
            s_nPendingPB   = (uint32) m68k_areg (regs, 0);
            s_nPendingTrap = s_nTraps;
        }

        // Naming only the file-system traps keeps the log readable: a boot
        // makes tens of thousands of Toolbox calls and the interesting ones
        // are the handful around the volume decision.
        const char *pName = TrapName (opcode);
        if (pName != 0 && s_nNamed < 600)
        {
            s_nNamed++;
            CLogger::Get ()->Write ("okapia-trap", LogNotice,
                                    "A%03X %s (trap #%u)",
                                    opcode & 0x0FFF, pName, s_nTraps);
        }
    }

    __real__Z9op_illg_1j (opcode);
}

void TrapReport (void)
{
    CLogger::Get ()->Write ("okapia-trap", LogNotice,
                            "%u A-line traps so far", s_nTraps);
}
