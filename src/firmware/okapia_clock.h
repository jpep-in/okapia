/*
 * okapia_clock.h — time zones, summer time, and the date said in words.
 *
 * A Macintosh of this era has no time zone: its clock *is* local time. NTP
 * answers in UTC, so Okapia has to know where it is — and a fixed offset is
 * wrong for half the year wherever clocks change. So a zone here is a standard
 * offset and a rule for summer time, picked from a short list by the cities
 * people recognise, as the Map control panel did.
 *
 * Pure: no Circle and no clock of its own, so tests/host checks every
 * transition on the development machine. Used by the boot menu (the list, the
 * date on the information page) and by clock_circle.cpp (the offset in force).
 *
 * The rules are the ones in force since 2007 in the United States and Canada,
 * since 1996 in the European Union, since 2008 in Australia and since 2007 in
 * New Zealand. Zones that have no summer time today have none here.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_clock_h
#define _okapia_clock_h

#include "okapia_strings.h"

// The zones, in the order the menu offers them: east of UTC first, then west.
unsigned    ClockZoneCount (void);
const char *ClockZoneKey (unsigned nZone);          // what the preferences carry
TStringId   ClockZoneName (unsigned nZone);         // the cities
int         ClockZoneStandard (unsigned nZone);     // minutes east of UTC, in winter

// The zone a preference names, or -1. Case is ignored.
int ClockZoneFind (const char *pKey);

// Minutes east of UTC in force at nUTC (seconds since 1970) in that zone.
int ClockZoneOffset (unsigned nZone, unsigned nUTC);

// "UTC", "UTC+1", "UTC+5:30", "UTC-3".
void ClockOffsetText (char *pOut, unsigned nSize, int nMinutes);

struct TClockDate
{
    unsigned nYear;
    unsigned nMonth;            // 1 to 12
    unsigned nDay;              // 1 to 31
    unsigned nWeekday;          // 0 is Sunday
    unsigned nHour, nMinute, nSecond;
};

void ClockDateOf (unsigned nSeconds, TClockDate *pDate);

// Seconds since 1970 for a calendar date at midnight.
unsigned ClockSecondsOf (unsigned nYear, unsigned nMonth, unsigned nDay);

// "Thursday 12 November 2026, 14:32", in the language the firmware speaks.
void ClockFormat (char *pOut, unsigned nSize, unsigned nLocal);

/*
 *  The clock set on the Macintosh
 *
 *  Without an RTC or the network, the time at startup is the latest date the
 *  card knows (docs/topics/clock-and-pram.md). A clock set back on purpose
 *  would lose to that every time, so what was set is kept on the card with the
 *  latest date known at that moment: as long as nothing on the card is later
 *  than that, the set clock is the one to believe.
 */
struct TClockRecord
{
    unsigned nSet;              // local seconds: what the clock said, last we knew
    unsigned nFloor;            // local seconds: the latest date known when it was set
};

// One line, "<set> <floor>\n". Answers the length.
unsigned ClockRecordFormat (char *pOut, unsigned nSize, const TClockRecord *pRecord);

// Strict: anything but two numbers on one line is refused, a short file first
// of all — it is what a write cut off by a pulled plug leaves.
bool ClockRecordParse (const char *pText, unsigned nLength, TClockRecord *pRecord);

// Whether a value read from the Macintosh's Time global can be a clock somebody
// set: from 1990 up to the last second HFS can hold, exclusive. A Macintosh
// starting writes zero there, and garbage is possible before its memory is set.
bool ClockMacPlausible (unsigned nMacSeconds);

// The Mac counts from 1904, Unix from 1970: 66 years with 17 leap days.
static const unsigned CLOCK_MAC_UNIX_DIFF = 2082844800U;

#endif
