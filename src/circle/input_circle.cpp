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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "okapia_circle.h"
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/devicenameservice.h>

#include "okapia_input.h"
#include <string.h>

#include "adb.h"
#include "main.h"

#define FROM "okapia-input"

static CUSBKeyboardDevice *s_pKeyboard;
static CMouseDevice       *s_pMouse;
static unsigned char       s_LastKeys[6];
static unsigned char       s_LastModifiers;
static unsigned            s_LastMouseButtons;

/*
 *  USB HID usage -> Mac ADB key code.
 *
 *  Not written by hand. SDL2 scancodes *are* USB HID usage IDs — SDL defines
 *  its scancode set from the HID Keyboard/Keypad page — so the SDL2 section of
 *  Basilisk's own keycodes file is already the table a USB host needs, and
 *  scripts/gen-keycodes.py turns it into the array below at build time.
 *
 *  Deriving it is not pedantry. The hand-written version this replaces had the
 *  four arrows wrong, because ADBKeyDown() wants *raw ADB* codes and the
 *  familiar numbers are the Mac virtual key codes — which agree for letters and
 *  digits and differ for the arrows. Up went out as 0x7E, landed beside the
 *  Power key, and opened the shutdown dialog on every press. The generated
 *  table also brings the numeric keypad, which the hand-written one omitted
 *  entirely.
 *
 *  A card may override it: see KeycodesLoad().
 */
static unsigned char s_HIDToMac[256] =
#include "keycodes_default.h"
;

/*
 *  Replace the table from a keycodes file on the card.
 *
 *  Same format as Basilisk's: a driver name on its own line, then "scancode
 *  keycode" pairs in decimal, # or ; for comments. The section is the one
 *  headed "sdl cocoa" — Basilisk's name for the SDL2 table — or "okapia" for a
 *  file written for this machine. Anything else in the file is skipped, so an
 *  unmodified BasiliskII.keycodes works as it stands.
 *
 *  Partial overrides are allowed and useful: a file with two lines fixes two
 *  keys and leaves the rest alone.
 */

bool KeycodesLoad (const char *pPath)
{
    FILE *pFile = fopen (pPath, "r");
    if (pFile == 0)
    {
        return false;
    }

    char Line[128];
    bool bInSection = false;
    bool bHeaderRun = false;        // reading a run of driver names, not pairs
    unsigned nApplied = 0;

    while (fgets (Line, sizeof Line, pFile) != 0)
    {
        char *p = strpbrk (Line, "#;\r\n");
        if (p != 0)
        {
            *p = '\0';
        }
        while (*Line != '\0' && isspace ((unsigned char) Line[strlen (Line) - 1]))
        {
            Line[strlen (Line) - 1] = '\0';
        }
        char *pStart = Line;
        while (isspace ((unsigned char) *pStart))
        {
            pStart++;
        }
        if (*pStart == '\0')
        {
            continue;
        }

        if (!isdigit ((unsigned char) *pStart))
        {
            // A driver name. Several may stack in front of one table, so a run
            // of them selects the section together.
            if (!bHeaderRun)
            {
                bInSection = false;
                bHeaderRun = true;
            }
            if (strcmp (pStart, "sdl cocoa") == 0 || strcmp (pStart, "okapia") == 0)
            {
                bInSection = true;
            }
            continue;
        }

        bHeaderRun = false;
        if (!bInSection)
        {
            continue;
        }

        int nScan = -1, nMac = -1;
        if (sscanf (pStart, "%d %d", &nScan, &nMac) != 2)
        {
            continue;
        }
        if (nScan < 0 || nScan > 255 || nMac < 0 || nMac > 255)
        {
            continue;
        }
        s_HIDToMac[nScan] = (unsigned char) nMac;
        nApplied++;
    }
    fclose (pFile);

    if (nApplied == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: no \"sdl cocoa\" or \"okapia\" section, keyboard unchanged",
                                pPath);
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "%s: %u key mappings applied", pPath, nApplied);
    return true;
}

static void PostKey (unsigned char nHID, bool bDown)
{
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

static void MouseStatusHandler (unsigned nButtons, int nDeltaX,
                                int nDeltaY, int nWheelMove)
{
    (void) nWheelMove;

    if (nDeltaX != 0 || nDeltaY != 0)
    {
        ADBMouseMoved (nDeltaX, nDeltaY);
    }

    static const unsigned ButtonMasks[] =
    {
        MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE
    };
    unsigned nChanged = nButtons ^ s_LastMouseButtons;
    for (unsigned i = 0; i < sizeof ButtonMasks / sizeof ButtonMasks[0]; i++)
    {
        if (nChanged & ButtonMasks[i])
        {
            if (nButtons & ButtonMasks[i]) ADBMouseDown (i); else ADBMouseUp (i);
        }
    }
    s_LastMouseButtons = nButtons;
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
        // Through the firmware's bridge and not straight to Circle: the boot
        // menu ran first and claimed the mouse, and CMouseDevice asserts on a
        // second registration with no way to withdraw the first (mouse.cpp:85).
        // The bridge holds that one registration and forwards from here on; it
        // claims the device itself on a boot where the firmware never ran.
        if (FwInputPassMouseTo (MouseStatusHandler))
        {
            s_pMouse = (CMouseDevice *)
                CDeviceNameService::Get ()->GetDevice ("mouse1", FALSE);
            ADBSetRelMouseMode (true);
            CLogger::Get ()->Write (FROM, LogNotice, "Mouse attached, relative mode");
        }
    }
}

void InputInit (void)
{
    memset (s_LastKeys, 0, sizeof s_LastKeys);
    s_LastModifiers = 0;
    s_LastMouseButtons = 0;
    InputAttachDevices ();
}
