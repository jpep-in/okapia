//
// okapia_firmware.h — the boot firmware, as the kernel sees it.
//
// It runs between the card being ready and the emulator starting: the
// preferences are read, the Mac's memory is allocated and USB is up by then,
// which is everything this needs. It claims the display, has its say, gives it
// back, and answers what should happen next.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_firmware_h
#define _okapia_firmware_h

enum TFirmwareResult
{
    FirmwareBoot,                       // carry on and start the Macintosh
    FirmwareHalt,                       // the user asked for the power off
    FirmwareReboot                      // a setting needs Okapia restarted
};

// Which emulator a kernel image carries. One per image — the two cores define
// the same symbols and cannot be linked together — so it is a fact about the
// image, handed in rather than read: the firmware stays a screen, and nothing
// in src/firmware knows that Basilisk or SheepShaver exist.
enum TFirmwareEngine
{
    FirmwareEngine68k,
    FirmwareEnginePowerPC
};

// Never fails in a way that stops the boot: a firmware that cannot draw must
// still let the Macintosh start, so every failure here warns and returns
// FirmwareBoot. The screen belongs to the Mac a moment later either way.
TFirmwareResult FirmwareRun (TFirmwareEngine Built);

// The startup the preferences state, as the firmware keeps them: read before
// and after the firmware runs at a Macintosh restart, it says whether the
// engine can restart in place. Both reads come from the firmware's own list,
// never the engine's: each engine has its copy (the PowerPC one renamed ppc__
// on the board, made local on a desktop), loaded once, normalised as it was
// loaded, and the firmware writes only the one it shares with the 68k engine.
struct TFirmwareStartup
{
    unsigned long long nVolumes;    // every disk and cdrom line, in order, '*' included
    char Sound[16];                 // the device is chosen as the engine starts
    bool bNoSound;
    int  nBootDriver;
};
void FirmwareStartupRead (TFirmwareStartup *pOut);
bool FirmwareStartupSame (const TFirmwareStartup &A, const TFirmwareStartup &B);

// The function that drains the board's log ring onto the card, which the
// Journal page calls before it reads the file so that it shows the lines logged
// since the last drain. Optional: the specimen has no board and passes none.
void FirmwareSetLogFlush (void (*pFlush) (void));

// The welcome page, before any workspace (a desktop's; okapia_welcome.h): what
// Okapia is, then a workspace opened or made, a ROM and a System added. Answers
// false when the person quit, true on Continue: the workspace is the card from
// then on, and the caller starts it, boot menu first.
bool FirmwareWelcome (void);

// Which emulator the startup volume asks for, once FirmwareRun has been. Equal
// to what was handed in unless the card says otherwise, and then it is the
// kernel's business — it is the only thing that can go and fetch another image.
TFirmwareEngine FirmwareWantedEngine (void);

#endif
