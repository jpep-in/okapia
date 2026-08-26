/*
 * input_circle.cpp — USB keyboard and mouse, presented to the Mac as ADB.
 *
 * Circle reports raw HID; the Mac wants Apple Desktop Bus key codes, which bear
 * no relation to HID numbering. The table below maps the keys a Mac keyboard
 * actually has, and unmapped keys are dropped rather than sent as something
 * else — a wrong key code is worse than a missing one.
 *
 * Events are pushed into adb.cpp and flagged with INTFLAG_ADB; the core then
 * calls ADBInterrupt() from its interrupt path (emul_op.cpp:512).
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/devicenameservice.h>
#include <string.h>

#include "adb.h"
#include "main.h"

#define FROM "okapia-input"

static CUSBKeyboardDevice *s_pKeyboard;
static CMouseDevice       *s_pMouse;
static unsigned char       s_LastKeys[6];
static unsigned char       s_LastModifiers;

/*
 *  USB HID usage -> Mac ADB key code. Index is the HID usage ID.
 *  0xFF means "no equivalent"; those keys are ignored.
 */
static const unsigned char s_HIDToMac[128] =
{
    /* 00-03 */ 0xFF, 0xFF, 0xFF, 0xFF,
    /* 04-0D  a b c d e f g h i j */ 0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26,
    /* 0E-17  k l m n o p q r s t */ 0x28, 0x25, 0x2E, 0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11,
    /* 18-1D  u v w x y z         */ 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06,
    /* 1E-27  1 2 3 4 5 6 7 8 9 0 */ 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19, 0x1D,
    /* 28 Return    */ 0x24,
    /* 29 Escape    */ 0x35,
    /* 2A Backspace */ 0x33,
    /* 2B Tab       */ 0x30,
    /* 2C Space     */ 0x31,
    /* 2D -  2E =   */ 0x1B, 0x18,
    /* 2F [  30 ]   */ 0x21, 0x1E,
    /* 31 backslash */ 0x2A,
    /* 32 (non-US)  */ 0x2A,
    /* 33 ;  34 '   */ 0x29, 0x27,
    /* 35 `         */ 0x32,
    /* 36 ,  37 .  38 / */ 0x2B, 0x2F, 0x2C,
    /* 39 CapsLock  */ 0x39,
    /* 3A-45 F1-F12 */ 0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62, 0x64, 0x65, 0x6D, 0x67, 0x6F,
    /* 46-4F        */ 0xFF, 0xFF, 0xFF, 0x72, 0x73, 0x74, 0x75, 0x77, 0x79, 0x7C,
    /* 50 Left 51 Down 52 Up */ 0x7B, 0x7D, 0x7E,
    /* 53-7F */ 0xFF
};

static void PostKey (unsigned char nHID, bool bDown)
{
    if (nHID >= sizeof s_HIDToMac)
    {
        return;
    }
    unsigned char nMac = s_HIDToMac[nHID];
    if (nMac == 0xFF)
    {
        return;                 // no Mac equivalent: drop it
    }

    if (bDown)
    {
        ADBKeyDown (nMac);
    }
    else
    {
        ADBKeyUp (nMac);
    }
    SetInterruptFlag (INTFLAG_ADB);
}

/*
 *  Circle hands us the full keyboard state, not events, so changes have to be
 *  derived by comparing with the previous report.
 */
static void KeyStatusHandler (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    // Modifiers, bit by bit: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI
    static const unsigned char ModToMac[8] =
    {
        0x36,   // left control -> Control
        0x38,   // left shift
        0x3A,   // left option
        0x37,   // left GUI -> Command
        0x36, 0x38, 0x3A, 0x37
    };
    for (unsigned i = 0; i < 8; i++)
    {
        unsigned char nBit = 1 << i;
        bool bWas = (s_LastModifiers & nBit) != 0;
        bool bIs  = (ucModifiers & nBit) != 0;
        if (bWas != bIs)
        {
            if (bIs) ADBKeyDown (ModToMac[i]); else ADBKeyUp (ModToMac[i]);
            SetInterruptFlag (INTFLAG_ADB);
        }
    }
    s_LastModifiers = ucModifiers;

    // Released: in the old report, absent from the new one.
    for (unsigned i = 0; i < 6; i++)
    {
        unsigned char k = s_LastKeys[i];
        if (k == 0) continue;
        bool bStillDown = false;
        for (unsigned j = 0; j < 6; j++)
        {
            if (RawKeys[j] == k) { bStillDown = true; break; }
        }
        if (!bStillDown) PostKey (k, false);
    }

    // Pressed: in the new report, absent from the old one.
    for (unsigned i = 0; i < 6; i++)
    {
        unsigned char k = RawKeys[i];
        if (k == 0) continue;
        bool bWasDown = false;
        for (unsigned j = 0; j < 6; j++)
        {
            if (s_LastKeys[j] == k) { bWasDown = true; break; }
        }
        if (!bWasDown) PostKey (k, true);
    }

    memcpy (s_LastKeys, RawKeys, sizeof s_LastKeys);
}

static void MouseEventHandler (TMouseEvent Event, unsigned nButtons,
                               unsigned nPosX, unsigned nPosY, int nWheelMove)
{
    switch (Event)
    {
    case MouseEventMouseMove:
        // Relative mode: Circle reports a displacement here.
        ADBMouseMoved ((int) nPosX, (int) nPosY);
        break;

    case MouseEventMouseDown:
        ADBMouseDown (nButtons & MOUSE_BUTTON_LEFT ? 0 : 1);
        break;

    case MouseEventMouseUp:
        ADBMouseUp (nButtons & MOUSE_BUTTON_LEFT ? 0 : 1);
        break;

    default:
        return;
    }
    SetInterruptFlag (INTFLAG_ADB);
}

/*
 *  Attach whatever is plugged in. Called repeatedly: USB devices may appear
 *  after boot, and Circle only reports them once enumerated.
 */

void InputAttachDevices (void)
{
    if (s_pKeyboard == 0)
    {
        s_pKeyboard = (CUSBKeyboardDevice *)
            CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
        if (s_pKeyboard != 0)
        {
            s_pKeyboard->RegisterKeyStatusHandlerRaw (KeyStatusHandler);
            CLogger::Get ()->Write (FROM, LogNotice, "Keyboard attached");
        }
    }

    if (s_pMouse == 0)
    {
        s_pMouse = (CMouseDevice *)
            CDeviceNameService::Get ()->GetDevice ("mouse1", FALSE);
        if (s_pMouse != 0)
        {
            s_pMouse->RegisterEventHandler (MouseEventHandler);
            // The Mac tracks its own pointer; we send movement, not position.
            ADBSetRelMouseMode (true);
            CLogger::Get ()->Write (FROM, LogNotice, "Mouse attached, relative mode");
        }
    }
}

void InputInit (void)
{
    memset (s_LastKeys, 0, sizeof s_LastKeys);
    s_LastModifiers = 0;
    InputAttachDevices ();
}
