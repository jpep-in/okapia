//
// okapia_rom.h — what a ROM file is, and which disks it can start.
//
// The rules of scripts/check-rom.py, in C++ so the welcome page can say at once
// what a file it is given will do (docs/topics/desktop.md):
//
// - 68k, for Basilisk II: the word at offset 8 must be 0x067C, 32-bit clean,
//   which DIRECT_ADDRESSING demands (BasiliskII/src/rom_patches.cpp:838), and
//   the longword at 0 must be the sum of every 16-bit word from offset 4. The
//   Quadra/Centris ROM of 1993 (F1ACAD13) is the one Okapia is tuned for
//   (docs/project/decisions.md).
// - PowerPC, for SheepShaver: a 4 MB image whose nanokernel name, at offset
//   0x30d064, is one of SheepShaver's six (Old World, Mac OS 7.5.2 and later);
//   or a "Mac OS ROM" file, <CHRP-BOOT> (New World), which starts 8.1 and later.
//
// And the disks: which ROM a System needs (HfsFlavourOf), and whether the ROMs
// at hand can start it — the installer's verdict, moved here
// (okapia-installer's verdict_for).
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_rom_h
#define _okapia_rom_h

enum TRomKind
{
    RomUnusable,                        // not a ROM either engine can start
    Rom68k,                             // 32-bit clean: Basilisk II's `rom`
    RomPowerPCOldWorld,                 // 4 MB, a known nanokernel: SheepShaver's `romppc`
    RomPowerPCNewWorld                  // <CHRP-BOOT>: `romppc`, Mac OS 8.1 and later
};

// Why a file is not usable, or that it is.
enum TRomVerdict
{
    RomUsable,
    RomNot32BitClean,                   // a Mac Plus, SE, Mac II ROM
    RomChecksumMismatch,                // 32-bit clean but damaged
    RomUnknown,                         // not a ROM anyone knows
    RomUnknownNanokernel,               // 4 MB, but not SheepShaver's
    RomTooSmall
};

struct TRomFacts
{
    TRomKind    Kind;
    TRomVerdict Verdict;
    unsigned    nBytes;
    unsigned    nChecksum;              // 68k: the stored longword at 0
    bool        bRecommended;           // the Quadra ROM; any usable PowerPC one
    // What it starts, as System codes (7.6.1 is 761): 701 to 810 for 68k,
    // 752 (Old World) or 810 (New World) to 904 for PowerPC; 0 when unusable.
    unsigned    nFirstSystem, nLastSystem;
    char        Identity[64];           // "Quadra/Centris 610, 650, 800 (1993)"
    char        Detail[48];             // "Gazelle", "32-bit clean", or why not
};

// Reads the file at a host path. False when it cannot be read at all.
bool RomIdentify (const char *pHostPath, TRomFacts *pFacts);

// The same from bytes already read (and the rules' tests).
void RomIdentifyBytes (const unsigned char *pData, unsigned long nBytes, TRomFacts *pFacts);

// Whether a file of this size and name is worth asking about: a directory
// listing has disk images and logs too.
bool RomLooksLikeOne (const char *pName, unsigned long nBytes);

// The latest System Okapia starts: Mac OS 9.0.4 (docs/topics/engine-powerpc.md).
static const unsigned ROM_LAST_SYSTEM = 904;

/*
 *  A disk, against the ROMs at hand
 */
enum TDiskVerdict
{
    DiskData,                           // not a startup disk: mounted as data
    DiskNoSystem,                       // HFS, no System Folder
    DiskUnreadable,                     // a System Folder whose version cannot be read
    DiskTooNew,                         // later than 9.0.4
    DiskStarts68k,
    DiskStartsPowerPC,
    DiskStartsBoth,                     // the boot menu chooses
    DiskNeeds68k,                       // a 68k System, no 68k ROM
    DiskNeedsPowerPC,                   // a PowerPC System, no PowerPC ROM
    DiskNeedsAny,
    DiskPowerPCTooOld                   // the PowerPC ROM starts later Systems only
};

struct TDiskJudgement
{
    TDiskVerdict Verdict;
    // The ROM that is there and of no use to this disk, for the sentence that
    // says so ("The PowerPC ROM cannot start it").
    bool         bOther68kUseless, bOtherPowerPCUseless;
    unsigned     nPowerPCFloor;         // DiskPowerPCTooOld: the System it needs
};

// nSystem: the System's code (0 when unreadable), its flavour from
// HfsFlavourOf (0 68k, 1 PowerPC, 2 universal, 3 unreadable, as given by the
// caller); p68k and pPowerPC the usable ROMs at hand, or 0.
enum TDiskFlavour { Flavour68k, FlavourPowerPC, FlavourUniversal, FlavourUnreadable };
TDiskJudgement DiskJudge (bool bHfs, bool bBootable, unsigned nSystem, TDiskFlavour Flavour,
                          const TRomFacts *p68k, const TRomFacts *pPowerPC);

// Whether the judgement lets the disk start.
bool DiskJudgementStarts (const TDiskJudgement &J);

#endif
