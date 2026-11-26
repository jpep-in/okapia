/*
 * check_clock.cpp — time zones, summer time, the date in words, and the clock
 * set on the Macintosh as it is kept on the card.
 *
 * A summer time rule off by a week is an hour wrong for a week a year, and
 * nobody looks at the menu bar closely enough on that Sunday to say why. Every
 * transition is decidable here, at the second.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "okapia_clock.h"
#include "okapia_strings.h"

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

static unsigned At (unsigned y, unsigned m, unsigned d, unsigned h, unsigned min)
{
    return ClockSecondsOf (y, m, d) + h * 3600 + min * 60;
}

static int Offset (const char *pZone, unsigned nUTC)
{
    return ClockZoneOffset ((unsigned) ClockZoneFind (pZone), nUTC);
}

// The offset just before and at a transition.
static void Transition (const char *pZone, unsigned nUTC, int nBefore, int nAfter,
                        const char *pWhat)
{
    Expect (Offset (pZone, nUTC - 1) == nBefore && Offset (pZone, nUTC) == nAfter, pWhat);
}

static void CheckCalendar (void)
{
    printf ("\nthe calendar\n");
    TClockDate D;
    ClockDateOf (0, &D);
    Expect (D.nYear == 1970 && D.nMonth == 1 && D.nDay == 1 && D.nWeekday == 4,
            "1 January 1970 was a Thursday");
    ClockDateOf (At (2026, 11, 12, 14, 5) + 9, &D);
    Expect (D.nYear == 2026 && D.nMonth == 11 && D.nDay == 12 && D.nWeekday == 4
            && D.nHour == 14 && D.nMinute == 5 && D.nSecond == 9,
            "12 November 2026 is a Thursday, and the time of day comes back");
    ClockDateOf (At (2024, 2, 29, 0, 0), &D);
    Expect (D.nMonth == 2 && D.nDay == 29, "29 February 2024 exists");
    ClockDateOf (At (2024, 2, 29, 0, 0) + 86400, &D);
    Expect (D.nMonth == 3 && D.nDay == 1, "and is followed by 1 March");
    Expect (ClockSecondsOf (2040, 2, 6) + 6 * 3600 + 28 * 60 + 15 + CLOCK_MAC_UNIX_DIFF
            == 0xFFFFFFFFu, "the Macintosh's last second is 6 February 2040, 06:28:15");
}

static void CheckZones (void)
{
    printf ("\nthe zones\n");
    bool bUnique = true;
    for (unsigned i = 0; i < ClockZoneCount (); i++)
    {
        bUnique = bUnique && ClockZoneFind (ClockZoneKey (i)) == (int) i;
    }
    Expect (bUnique, "every key finds its own zone");
    Expect (ClockZoneFind ("Paris") == ClockZoneFind ("paris"), "case is ignored");
    Expect (ClockZoneFind ("atlantis") < 0 && ClockZoneFind (0) < 0 && ClockZoneFind ("") < 0,
            "an unknown key, a missing one and an empty one find nothing");
    Expect (ClockZoneCount () <= 23, "the list fits a menu as long as the countries'");

    // The European Union: 01:00 UTC on the last Sunday of March and October,
    // the same instant in every zone (Directive 2000/84/EC).
    Transition ("paris", At (2026, 3, 29, 1, 0), 60, 120, "Paris: summer time on 29 March 2026, 01:00 UTC");
    Transition ("paris", At (2026, 10, 25, 1, 0), 120, 60, "and back on 25 October, 01:00 UTC");
    Transition ("london", At (2026, 3, 29, 1, 0), 0, 60, "London on the same instant");
    Transition ("athens", At (2026, 10, 25, 1, 0), 180, 120, "and Athens");
    Transition ("paris", At (2027, 3, 28, 1, 0), 60, 120, "Paris in 2027: 28 March");

    // The United States and Canada: 02:00 local, second Sunday of March and
    // first of November (Energy Policy Act of 2005).
    Transition ("newyork", At (2026, 3, 8, 7, 0), -300, -240, "New York: 8 March 2026, 02:00 EST");
    Transition ("newyork", At (2026, 11, 1, 6, 0), -240, -300, "and 1 November, 02:00 EDT");
    Transition ("losangeles", At (2026, 3, 8, 10, 0), -480, -420, "Los Angeles at its own 02:00");

    // Australia's south-east: first Sunday of October and of April.
    Expect (Offset ("sydney", At (2026, 1, 15, 0, 0)) == 660
            && Offset ("sydney", At (2026, 7, 15, 0, 0)) == 600,
            "Sydney: summer in January, standard in July");
    Transition ("sydney", At (2026, 4, 4, 16, 0), 660, 600, "Sydney: back on 5 April 2026, 03:00 AEDT");
    Transition ("sydney", At (2026, 10, 3, 16, 0), 600, 660, "forward on 4 October, 02:00 AEST");

    // New Zealand: last Sunday of September, first Sunday of April.
    Transition ("auckland", At (2026, 4, 4, 14, 0), 780, 720, "Auckland: back on 5 April 2026");
    Transition ("auckland", At (2026, 9, 26, 14, 0), 720, 780, "forward on 27 September");

    Expect (Offset ("brisbane", At (2026, 1, 15, 0, 0)) == 600
            && Offset ("tokyo", At (2026, 7, 15, 0, 0)) == 540
            && Offset ("delhi", At (2026, 7, 15, 0, 0)) == 330,
            "zones without summer time keep their offset all year");

    char Text[16];
    ClockOffsetText (Text, sizeof Text, 0);
    Expect (strcmp (Text, "UTC") == 0, "UTC is UTC");
    ClockOffsetText (Text, sizeof Text, 60);
    Expect (strcmp (Text, "UTC+1") == 0, "UTC+1");
    ClockOffsetText (Text, sizeof Text, 330);
    Expect (strcmp (Text, "UTC+5:30") == 0, "UTC+5:30");
    ClockOffsetText (Text, sizeof Text, -180);
    Expect (strcmp (Text, "UTC-3") == 0, "UTC-3");
}

static void CheckFormat (void)
{
    printf ("\nthe date in words\n");
    char Text[96];
    const TLanguage Was = StringsLanguage ();
    StringsSetLanguage (LangEN);
    ClockFormat (Text, sizeof Text, At (2026, 11, 12, 14, 5));
    Expect (strcmp (Text, "Thursday 12 November 2026, 14:05") == 0, Text);
    StringsSetLanguage (LangFR);
    ClockFormat (Text, sizeof Text, At (2026, 3, 1, 9, 0));
    Expect (strcmp (Text, "dimanche 1 mars 2026, 09:00") == 0, Text);
    StringsSetLanguage (Was);
}

static void CheckRecord (void)
{
    printf ("\nthe clock set on the Macintosh\n");
    TClockRecord R = { 1700000000u, 1800000000u };
    char Text[32];
    const unsigned n = ClockRecordFormat (Text, sizeof Text, &R);
    TClockRecord Back = { 0, 0 };
    Expect (ClockRecordParse (Text, n, &Back) && Back.nSet == R.nSet && Back.nFloor == R.nFloor,
            "what is written reads back");
    Expect (!ClockRecordParse (Text, n - 1, &Back), "a write cut before its newline is refused");
    Expect (!ClockRecordParse ("17 18", 5, &Back), "so is one with no newline at all");
    Expect (!ClockRecordParse ("17\n", 3, &Back), "one number is not a record");
    Expect (!ClockRecordParse ("17 18\nx", 7, &Back), "nothing may follow");
    Expect (!ClockRecordParse ("99999999999 1\n", 14, &Back), "a value past 32 bits is refused");
    Expect (!ClockRecordParse ("", 0, &Back), "an empty file is refused");

    Expect (!ClockMacPlausible (0), "a Time global of zero is not a clock somebody set");
    Expect (!ClockMacPlausible (At (1989, 12, 31, 23, 59) + CLOCK_MAC_UNIX_DIFF), "nor 1989");
    Expect (ClockMacPlausible (At (2026, 11, 12, 14, 5) + CLOCK_MAC_UNIX_DIFF), "2026 is");
    Expect (!ClockMacPlausible (0xFFFFFFFFu), "the saturated value is not");
}

int main (void)
{
    CheckCalendar ();
    CheckZones ();
    CheckFormat ();
    CheckRecord ();

    if (s_nFailures != 0)
    {
        printf ("%u failure(s)\n", s_nFailures);
        return 1;
    }
    printf ("all clock checks passed\n");
    return 0;
}
