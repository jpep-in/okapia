//
// okapia_keys.h — a USB usage, as the boot menu's logical key.
//
// Usages are what a USB keyboard reports on the Pi, and also what SDL calls its
// scancodes (SDL defines its set from the HID Keyboard/Keypad page), so both
// hosts turn them into OkKey values here, and the screens never see a usage.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_keys_h
#define _okapia_keys_h

#include "okapia_event.h"

static inline unsigned KeyOfUsage (unsigned nUsage)
{
    switch (nUsage)
    {
    case 0x2A:  return OkKeyBackspace;
    case 0x4C:  return OkKeyDelete;
    case 0x2B:  return OkKeyTab;
    case 0x2C:  return OkKeySpace;
    case 0x28:  return OkKeyReturn;
    case 0x58:  return OkKeyReturn;       // the keypad's Enter is the same promise
    case 0x29:  return OkKeyEscape;
    case 0x52:  return OkKeyUp;
    case 0x51:  return OkKeyDown;
    case 0x50:  return OkKeyLeft;
    case 0x4F:  return OkKeyRight;
    case 0x4A:  return OkKeyHome;
    case 0x4D:  return OkKeyEnd;
    case 0x4B:  return OkKeyPageUp;
    case 0x4E:  return OkKeyPageDown;
    default:    return OkKeyNone;
    }
}

// The usages the power-on window watches by name: Command-Option-P-R.
static const unsigned char UsageP = 0x13;
static const unsigned char UsageR = 0x15;

#endif
