//
// okapia_input.cpp — the firmware's own keyboard and mouse, before the Mac has any.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_input.h"

#include <circle/devicenameservice.h>
#include <circle/input/keymap.h>
#include <circle/input/mouse.h>
#include <circle/input/mousebehaviour.h>
#include <circle/logger.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/util.h>

#define FROM "firmware"

// Deep enough that a slice of the loop can be spent drawing without losing
// anything, small enough to be a static: a full queue would drop a key release
// and leave a control held down for ever, which is the failure worth avoiding.
static const unsigned QUEUE = 32;

static volatile TEvent   s_Queue[QUEUE];
static volatile unsigned s_nHead;               // written by the handlers
static volatile unsigned s_nTail;               // read by the loop

static int      s_nX, s_nY;                     // the pointer, in surface pixels
static int      s_nMaxX, s_nMaxY;
static unsigned s_nButtons;
static unsigned s_nModifiers;                   // live, for the events

static unsigned char s_LastKeys[6];
static volatile unsigned      s_nSeenModifiers;
static volatile unsigned char s_SeenKeys[32];   // one bit per USB usage
static volatile bool          s_bSeenAnything;

// Circle's own keyboard map, so that a French keyboard types what is written on
// it. Built with new rather than as a static: its constructor reads
// CKernelOptions, which does not exist until the kernel is being built, and a
// static one would assert before serial — the failure with no output at all.
//
// It is asked only for the character. The navigation keys come from the USB
// usage identifier instead, because those are physical and mean the same thing
// on every layout: Tab is Tab wherever the letters have moved to.
static CKeyMap *s_pKeyMap;

static CUSBKeyboardDevice *s_pKeyboard;
static CMouseDevice       *s_pMouse;

// Who the reports go to once the Macintosh has started. Circle's mouse can be
// claimed once and never withdrawn (mouse.cpp:85), so the one registration is
// made here and the reports are forwarded rather than re-routed.
static TMouseStatusHandler *s_pMouseNext;

// Nothing here waits: a handler runs at interrupt level and a queue that is full
// is a queue the loop is not draining, which dropping one event cannot fix.
static void Post (TEventType Type, unsigned nKey, unsigned nChar, int nX, int nY)
{
    const unsigned nNext = (s_nHead + 1) % QUEUE;
    if (nNext == s_nTail)
    {
        return;
    }
    s_Queue[s_nHead].Type       = Type;
    s_Queue[s_nHead].nKey       = nKey;
    s_Queue[s_nHead].nChar      = nChar;
    s_Queue[s_nHead].nModifiers = s_nModifiers;
    s_Queue[s_nHead].nX         = nX;
    s_Queue[s_nHead].nY         = nY;
    s_nHead = nNext;
}

// USB usage identifiers stop here. Everything above speaks of OkKeyTab.
static unsigned LogicalKey (unsigned char ucUsage)
{
    switch (ucUsage)
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

// Left and right alike: a keyboard's two Option keys mean the same thing, and
// somebody holding the right one is not asking for something else.
static unsigned Modifiers (unsigned char ucRaw)
{
    unsigned n = 0;
    if (ucRaw & (0x01 | 0x10))  n |= ModControl;
    if (ucRaw & (0x02 | 0x20))  n |= ModShift;
    if (ucRaw & (0x04 | 0x40))  n |= ModOption;
    if (ucRaw & (0x08 | 0x80))  n |= ModCommand;
    return n;
}

static void KeyHandler (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    s_nModifiers     = Modifiers (ucModifiers);
    s_nSeenModifiers = s_nSeenModifiers | s_nModifiers;
    if (ucModifiers != 0)
    {
        s_bSeenAnything = true;
    }

    // Circle hands over the whole state, not events. A key is newly down when it
    // is in this report and was not in the last one — which is also what makes
    // a key held down produce one event and not a hundred.
    for (unsigned i = 0; i < 6; i++)
    {
        const unsigned char ucKey = RawKeys[i];
        if (ucKey == 0)
        {
            continue;
        }
        s_bSeenAnything = true;
        s_SeenKeys[ucKey >> 3] = (unsigned char) (s_SeenKeys[ucKey >> 3] | (1 << (ucKey & 7)));

        bool bWasDown = false;
        for (unsigned j = 0; j < 6; j++)
        {
            if (s_LastKeys[j] == ucKey)
            {
                bWasDown = true;
                break;
            }
        }
        if (!bWasDown)
        {
            const unsigned nKey = LogicalKey (ucKey);
            unsigned nChar = 0;
            if (s_pKeyMap != 0)
            {
                const unsigned nLogical = s_pKeyMap->Translate (ucKey, ucModifiers);
                if (nLogical >= 0x20 && nLogical < 0x100 && nLogical != 0x7F)
                {
                    nChar = nLogical;   // Latin-1, which is also its code point
                }
                else if (nLogical == KeySpace)
                {
                    // Circle files the space bar under its special keys, at
                    // 0x100, so the ordinary filter drops it — and a field then
                    // takes every letter and refuses the one key between words.
                    nChar = ' ';
                }
            }
            if (nKey != OkKeyNone || nChar != 0)
            {
                Post (EventKeyDown, nKey, nChar, s_nX, s_nY);
            }
        }
    }
    memcpy (s_LastKeys, RawKeys, sizeof s_LastKeys);
}

// A pointer that moves one pixel per count crosses a 1280-pixel screen only
// with several sweeps of the hand. Every system has answered this the same way
// since the mouse existed: aim precisely when moving slowly, cover ground when
// moving fast. The threshold is what keeps a careful click from being thrown
// off, and it is deliberately low — the hand is never as slow as it thinks.
static int Accelerate (int nDelta)
{
    const int n = nDelta < 0 ? -nDelta : nDelta;
    return n <= 2 ? nDelta : nDelta * 2;
}

static void MouseHandler (unsigned nButtons, int nDX, int nDY, int nWheel)
{
    if (s_pMouseNext != 0)
    {
        (*s_pMouseNext) (nButtons, nDX, nDY, nWheel);
        return;
    }

    if (nDX != 0 || nDY != 0)
    {
        s_nX += Accelerate (nDX);
        s_nY += Accelerate (nDY);
        if (s_nX < 0)       s_nX = 0;
        if (s_nY < 0)       s_nY = 0;
        if (s_nX > s_nMaxX) s_nX = s_nMaxX;
        if (s_nY > s_nMaxY) s_nY = s_nMaxY;
        Post (EventMouseMove, OkKeyNone, 0, s_nX, s_nY);
    }

    // The left button and nothing else. A boot menu with a context menu would
    // be a boot menu nobody could use with the trackball of a period machine.
    const unsigned nWas = s_nButtons & MOUSE_BUTTON_LEFT;
    const unsigned nNow = nButtons   & MOUSE_BUTTON_LEFT;
    if (nWas != nNow)
    {
        Post (nNow ? EventMouseDown : EventMouseUp, OkKeyNone, 0, s_nX, s_nY);
        s_bSeenAnything = true;
    }
    s_nButtons = nButtons;
}

// Once and once only, whoever asks first.
static void ClaimMouse (void)
{
    if (s_pMouse != 0)
    {
        return;
    }
    s_pMouse = (CMouseDevice *) CDeviceNameService::Get ()->GetDevice ("mouse1", FALSE);
    if (s_pMouse != 0)
    {
        s_pMouse->RegisterStatusHandler (MouseHandler);
    }
}

bool FwInputWatch (void)
{
    if (s_pKeyboard != 0)
    {
        return true;                    // already watching; do not lose the latch
    }

    s_nHead = s_nTail = 0;
    s_nModifiers = 0;
    s_nSeenModifiers = 0;
    s_bSeenAnything = false;
    s_nButtons = 0;
    memset (s_LastKeys, 0, sizeof s_LastKeys);
    memset ((void *) s_SeenKeys, 0, sizeof s_SeenKeys);

    if (s_pKeyMap == 0)
    {
        s_pKeyMap = new CKeyMap;
    }

    s_pKeyboard = (CUSBKeyboardDevice *)
        CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
    if (s_pKeyboard != 0)
    {
        s_pKeyboard->RegisterKeyStatusHandlerRaw (KeyHandler);
    }

    // Raw, never Setup(): the cooked mouse drops every report until it is given
    // screen dimensions, and then reports absolute coordinates. The firmware
    // wants the displacements, and it draws its own pointer.
    s_pMouseNext = 0;
    ClaimMouse ();

    CLogger::Get ()->Write (FROM, LogNotice, "Watching: %s, %s",
                            s_pKeyboard != 0 ? "keyboard" : "no keyboard",
                            s_pMouse != 0 ? "mouse" : "no mouse");
    return s_pKeyboard != 0;
}

void FwInputBounds (unsigned nWidth, unsigned nHeight)
{
    s_nMaxX = (int) nWidth  - 1;
    s_nMaxY = (int) nHeight - 1;
    s_nX = s_nMaxX / 2;
    s_nY = s_nMaxY / 2;
}

bool FwInputPassMouseTo (TMouseStatusHandler *pHandler)
{
    ClaimMouse ();                      // for the boot where the firmware never ran
    s_pMouseNext = pHandler;
    return s_pMouse != 0;
}

bool FwInputNext (TEvent *pOut)
{
    if (s_nTail == s_nHead)
    {
        return false;
    }
    // Field by field: a volatile struct cannot simply be copied, and the
    // volatile is the point — the handlers that fill this run on an interrupt.
    pOut->Type       = s_Queue[s_nTail].Type;
    pOut->nKey       = s_Queue[s_nTail].nKey;
    pOut->nChar      = s_Queue[s_nTail].nChar;
    pOut->nModifiers = s_Queue[s_nTail].nModifiers;
    pOut->nX         = s_Queue[s_nTail].nX;
    pOut->nY         = s_Queue[s_nTail].nY;
    s_nTail = (s_nTail + 1) % QUEUE;
    return true;
}

void FwInputPointer (int *pX, int *pY)
{
    *pX = s_nX;
    *pY = s_nY;
}

unsigned FwInputSeenModifiers (void)
{
    return s_nSeenModifiers;
}

bool FwInputSeenKey (unsigned char ucUsage)
{
    return (s_SeenKeys[ucUsage >> 3] & (1 << (ucUsage & 7))) != 0;
}

bool FwInputSeenAnything (void)
{
    return s_bSeenAnything;
}
