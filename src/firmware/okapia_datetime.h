/*
 * okapia_datetime.h — the date and time, a group of the settings page.
 *
 * What time Okapia thinks it is, where that came from, and the two things a
 * Macintosh of this era cannot be told: where in the world it is, and whom to
 * ask the time. Setting the clock itself stays the Macintosh's — its Date &
 * Time control panel does it, and Okapia keeps what it sets
 * (docs/topics/clock-and-pram.md) — and the group offers no second way that
 * would disagree with the first.
 *
 * Like the other pages it knows nothing of Circle or of a preferences file: it
 * is handed the time and a sentence saying where it came from, and answers
 * values. tests/host drives it with synthetic events.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_datetime_h
#define _okapia_datetime_h

#include "okapia_widgets.h"

// A time server's name or address.
static const unsigned DATETIME_SERVER = 64;

struct TDateTimeValues
{
    // ClockZoneFind's index, or ClockZoneCount() for the fixed offset an older
    // card carries (`timezone`), which is offered only when the card had one.
    unsigned nZone;
    int      nFixedMinutes;
    bool     bNetTime;                  // ask a time server once the network is up
    char     Server[DATETIME_SERVER];   // kept when unticked, as the shared folder's path
};

struct TDateTime
{
    TDateTimeValues V;
    TDateTimeValues Opened;
    unsigned nLocal;                    // the time now, local seconds since 1970
    char     Source[96];                // where it came from, said whole by the caller
    // The machine keeps the time itself (a desktop): the group shows the date
    // and its source only, the zone and the time server being the host's.
    bool     bHostClock;
};

struct TPage;

// The group "Date & Time", laid out in a page that has one: the date and where
// it came from, the zone, and the time server. Its components go into that
// page's array; nLabel is the width of the label column it shares with the
// page, so that its fields line up with the others.
void DateTimeLayOut (TPage *pPage, TDateTime *pDateTime, unsigned nLabel);

// The model into the components, the date and its source included: called
// again when the minute turns.
void DateTimeSync (TDateTime *pDateTime);

enum TDateTimeAction
{
    DateTimeNothing,
    DateTimeZoneChanged,                // the clock may move now
    DateTimeServerChanged,              // ticked or unticked
    DateTimeHelp                        // the (i) at the end of the date's line
};

TDateTimeAction DateTimeOperate (TDateTime *pDateTime, int nIndex);

#endif
