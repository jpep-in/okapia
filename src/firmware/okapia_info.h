/*
 * okapia_info.h — what Okapia found, said on the screen instead of on a wire.
 *
 * Every line of this exists already: it goes out of the serial port at startup.
 * But the serial port is a wire nobody has once the card is in the Pi, which is
 * the same problem the preferences file has — information held prisoner behind
 * a physical connection. So the pane says nothing new; it says it where it can
 * be read.
 *
 * The lines are handed in whole. This screen does not know what a ROM is, or a
 * card, or a display: it lays out labels and values and nothing else, which is
 * what lets it be looked at without any of those existing.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_info_h
#define _okapia_info_h

#include "okapia_widgets.h"

static const unsigned INFO_MAX   = 8;
static const unsigned INFO_LABEL = 40;
static const unsigned INFO_VALUE = 96;

struct TInfoLine
{
    char Label[INFO_LABEL];
    char Value[INFO_VALUE];
};

// What Okapia is made of, which is a different question from what it found —
// hence a section of its own under a rule rather than more lines of the same.
static const unsigned INFO_CREDITS = 8;

struct TInfo
{
    TInfoLine Lines[INFO_MAX];
    unsigned  nCount;
    TInfoLine Credits[INFO_CREDITS];
    unsigned  nCredits;
};

// Copies both in, truncating rather than refusing: a value one character too
// long is worth reading, and a pane that will not open is worth nothing.
void InfoAdd (TInfo *pInfo, const char *pLabel, const char *pValue);

// A component and who it belongs to. The licences are the reason this pane owes
// them a place: the combined work is distributable only under the strictest of
// them, and a machine that says nothing about what it is made of is a machine
// asking to be taken on trust.
void InfoCredit (TInfo *pInfo, const char *pWhat, const char *pWho);

void InfoDraw (TSurface *pSurface, TInfo *pInfo);
void InfoRepaint (TSurface *pSurface);
unsigned InfoWidgets (TWidget **ppList);

// The frame the last layout drew, so a test can check that nothing strayed
// into its border. Every element spilling out was invisible in the numbers
// until the numbers were asked for.
TRect InfoDialog (void);

// The only thing this screen can be asked: to go away. It answers true for the
// button that does it, so the caller has nothing to remember.
bool InfoIsBack (int nIndex);

#endif
