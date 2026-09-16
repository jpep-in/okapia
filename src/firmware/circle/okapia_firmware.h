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

// Which emulator the startup volume asks for, once FirmwareRun has been. Equal
// to what was handed in unless the card says otherwise, and then it is the
// kernel's business — it is the only thing that can go and fetch another image.
TFirmwareEngine FirmwareWantedEngine (void);

#endif
