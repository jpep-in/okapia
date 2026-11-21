// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// The keyboard as raw HID reports: the desktop builds them from SDL's
// scancodes, which are HID usages (board_desktop.cpp).
#ifndef _circle_usb_usbkeyboard_h
#define _circle_usb_usbkeyboard_h
typedef void TKeyStatusHandlerRaw (unsigned char ucModifiers, const unsigned char RawKeys[6]);
class CUSBKeyboardDevice
{
public:
    void RegisterKeyStatusHandlerRaw (TKeyStatusHandlerRaw *pHandler);
};
#endif
