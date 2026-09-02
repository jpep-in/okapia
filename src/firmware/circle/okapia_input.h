//
// okapia_input.h — the firmware's own keyboard and mouse, before the Mac has any.
//
// This is the only file of the firmware that knows what a USB report looks
// like. Above it there are events with logical keys (okapia_event.h), which is
// what lets the screens and their tests be written without a keyboard in sight.
//
// The keyboard's handover to the emulator costs nothing: Circle keeps one raw
// handler, so InputInit() replacing ours in StartMacintosh() *is* the handover.
// Trying to give it back by registering a null handler does not detach it, it
// returns it to cooked mode and the boot stops dead — see AGENTS.md.
//
// The mouse is not like that. CMouseDevice::RegisterStatusHandler asserts that
// the slot is empty (mouse.cpp:85) and offers no way to withdraw, so it can be
// claimed exactly once in the life of a boot. This file therefore keeps that
// one registration and passes the reports on — FwInputPassMouseTo below.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_input_h
#define _okapia_input_h

#include "okapia_event.h"

#include <circle/input/mouse.h>

// Attaches whatever is plugged in and puts the pointer in the middle of a
// surface of this size. Answers false when there is no keyboard, which is worth
// knowing and is not an error: a machine wired to a screen alone still boots.
bool FwInputBegin (unsigned nWidth, unsigned nHeight);

// The next event, or false when there is none. Reports arrive by interrupt, so
// this is the only thing the loop has to call — and it must be called often
// enough that the queue does not fill, which for a menu means every slice.
bool FwInputNext (TEvent *pOut);

// Where the pointer is now. The loop asks in order to draw it; the events carry
// the same coordinates.
void FwInputPointer (int *pX, int *pY);

/*
 *  What the two-second window saw
 *
 *  Not what is held at the instant one asks. A report arrives on every change,
 *  release included, so a combination a human holds is almost never present
 *  whole in any single one of them — asking for that coincidence is how the
 *  first version of the window missed the combination it was watching for.
 *  A Macintosh sampled its keyboard through the window too.
 */
unsigned FwInputSeenModifiers (void);           // TModifier bits, latched
bool     FwInputSeenKey (unsigned char ucUsage);// by USB usage: this is the one
                                                // place where those still mean
                                                // something
bool     FwInputSeenAnything (void);

// Sends the mouse reports to pHandler from now on, claiming Circle's mouse first
// if the firmware never ran. Answers false when the machine has no mouse. After
// this the firmware queues no more pointer events, which is right: the Macintosh
// owns the pointer from the moment it starts.
bool FwInputPassMouseTo (TMouseStatusHandler *pHandler);

#endif
