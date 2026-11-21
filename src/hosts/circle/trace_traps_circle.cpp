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
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include "cpu_emulation.h"
#include "macos_util.h"
#include "readcpu.h"
#include "newcpu.h"

extern "C" void __real__Z9op_illg_1j (unsigned int opcode);

// A Control or Status parameter block (CntrlParam): csCode says what is being
// asked of the driver, ioResult what it answered. Offsets from Inside Macintosh.
enum
{
    pbIoResult  = 16,
    pbIoCRefNum = 24,
    pbCsCode    = 26
};

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
    case 0:            return "noErr";
    case ioErr:        return "ioErr";
    case nsvErr:       return "nsvErr";
    case paramErr:     return "paramErr";
    case wPrErr:       return "wPrErr";
    case permErr:      return "permErr";
    case nsDrvErr:     return "nsDrvErr (no such drive)";
    case extFSErr:     return "extFSErr";
    case noDriveErr:   return "noDriveErr";
    case offLinErr:    return "offLinErr";
    case controlErr:   return "controlErr (driver refused the call)";
    case statusErr:    return "statusErr (driver refused the call)";
    case volOnLinErr:  return "volOnLinErr (already mounted)";
    case noMacDskErr:  return "noMacDskErr";
    case badMDBErr:    return "badMDBErr (bad master directory block)";
    case wrPermErr:    return "wrPermErr";
    case memFullErr:   return "memFullErr";
    default:           return "?";
    }
}

static unsigned s_nTraps;
static unsigned s_nLogged;

// Several driver calls can be in flight at once, so watch a few at a time.
struct TPending
{
    uint32      nPB;
    const char *pWhat;
    unsigned    nCsCode;
    unsigned    nTrap;
};
static TPending s_Pending[8];

static void WatchPB (uint32 nPB, const char *pWhat, unsigned nCsCode)
{
    for (unsigned i = 0; i < 8; i++)
    {
        if (s_Pending[i].nPB == 0)
        {
            s_Pending[i].nPB     = nPB;
            s_Pending[i].pWhat   = pWhat;
            s_Pending[i].nCsCode = nCsCode;
            s_Pending[i].nTrap   = s_nTraps;
            return;
        }
    }
}

// ioResult holds 1 while a call is in progress and the OSErr once it settles.
static void ReapPending (void)
{
    for (unsigned i = 0; i < 8; i++)
    {
        if (s_Pending[i].nPB == 0)
        {
            continue;
        }
        int16 nResult = (int16) ReadMacInt16 (s_Pending[i].nPB + pbIoResult);
        if (nResult != 1)
        {
            if (s_nLogged < 800)
            {
                s_nLogged++;
                CLogger::Get ()->Write ("okapia-trap", LogNotice,
                                        "  -> %s csCode %u (trap #%u) = %d  %s",
                                        s_Pending[i].pWhat, s_Pending[i].nCsCode,
                                        s_Pending[i].nTrap, (int) nResult,
                                        ErrName (nResult));
            }
            s_Pending[i].nPB = 0;
        }
    }
}

extern "C" void __wrap__Z9op_illg_1j (unsigned int opcode)
{
    if ((opcode & 0xF000) == 0xA000)
    {
        s_nTraps++;
        ReapPending ();

        const unsigned nTrap = opcode & 0x0FFF;
        const uint32   nPB   = (uint32) m68k_areg (regs, 0);

        // Only the calls that can explain a refused mount: the mount itself,
        // and everything the Mac asks of the disk driver. Reads and writes are
        // the bulk of the traffic and say nothing here.
        switch (nTrap)
        {
        case 0x000F:
            WatchPB (nPB, "MountVol", 0);
            break;

        case 0x0004:
            WatchPB (nPB, "Control", (unsigned) ReadMacInt16 (nPB + pbCsCode));
            break;

        case 0x0005:
            WatchPB (nPB, "Status", (unsigned) ReadMacInt16 (nPB + pbCsCode));
            break;

        default:
            break;
        }
    }

    __real__Z9op_illg_1j (opcode);
}

void TrapReport (void)
{
    CLogger::Get ()->Write ("okapia-trap", LogNotice,
                            "%u A-line traps so far", s_nTraps);
}
