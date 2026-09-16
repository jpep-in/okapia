/*
 * okapia_event.h — what the firmware knows about the user, and nothing more.
 *
 * An event is the Mac's EventRecord with the parts a boot menu has no use for
 * taken out: no window, no message field doing four jobs at once, no queue of
 * activate and update events, because nothing here overlaps anything. What is
 * left is what a modal dialogue always actually read — a key, a pointer, and
 * which modifiers were down.
 *
 * The keys are logical. USB usage identifiers stop at the Circle side of the
 * firmware (circle/okapia_input.cpp), so that the screens and their tests speak
 * of OkKeyTab rather than 0x2B, and so that the host tests can inject events
 * without a keyboard anywhere in sight.
 *
 * The Ok prefix is not decoration: Circle's own keymap.h declares KeyTab,
 * KeyReturn and the rest at namespace scope, and a firmware file that includes
 * a Circle header would not compile beside them.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_event_h
#define _okapia_event_h

enum TKey
{
    OkKeyNone = 0,
    OkKeyTab,
    OkKeyBackspace,
    OkKeyDelete,
    OkKeySpace,
    OkKeyReturn,                          // Return and Enter alike
    OkKeyEscape,
    OkKeyUp,
    OkKeyDown,
    OkKeyLeft,
    OkKeyRight,
    OkKeyHome,
    OkKeyEnd,
    OkKeyPageUp,
    OkKeyPageDown
};

enum TModifier
{
    ModShift   = 1u << 0,
    ModControl = 1u << 1,
    ModOption  = 1u << 2,
    ModCommand = 1u << 3
};

enum TEventType
{
    EventNone = 0,
    EventKeyDown,
    EventMouseMove,
    EventMouseDown,
    EventMouseUp
};

struct TEvent
{
    TEventType Type;
    unsigned   nKey;                    // TKey, for EventKeyDown
    // The character it produced, as a code point, or 0 when it produced none.
    // It comes from the keyboard layout the machine is configured with, so a
    // French keyboard types what is written on it — which is the whole reason
    // this is not derived from the usage identifier here.
    unsigned   nChar;
    unsigned   nModifiers;              // TModifier bits, on every event
    int        nX;                      // pointer, for the mouse events
    int        nY;
};

#endif
