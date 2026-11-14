/*
 * okapia_clock.cpp — time zones, summer time, and the date said in words. See
 * okapia_clock.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_clock.h"

enum TSummerRule
{
    SummerNone,
    SummerEU,           // last Sunday of March to last Sunday of October, 01:00 UTC
    SummerUS,           // second Sunday of March to first Sunday of November, 02:00 local
    SummerAU,           // first Sunday of October to first Sunday of April, 02:00 standard
    SummerNZ            // last Sunday of September to first Sunday of April, 02:00 standard
};

struct TZone
{
    const char *pKey;
    int         nStandard;
    TSummerRule Rule;
    TStringId   Name;
};

static const TZone ZONES[] =
{
    { "utc",        0,    SummerNone, StrZoneUtc },
    { "london",     0,    SummerEU,   StrZoneLondon },
    { "paris",      60,   SummerEU,   StrZoneParis },
    { "athens",     120,  SummerEU,   StrZoneAthens },
    { "moscow",     180,  SummerNone, StrZoneMoscow },
    { "dubai",      240,  SummerNone, StrZoneDubai },
    { "delhi",      330,  SummerNone, StrZoneDelhi },
    { "bangkok",    420,  SummerNone, StrZoneBangkok },
    { "beijing",    480,  SummerNone, StrZoneBeijing },
    { "tokyo",      540,  SummerNone, StrZoneTokyo },
    { "brisbane",   600,  SummerNone, StrZoneBrisbane },
    { "sydney",     600,  SummerAU,   StrZoneSydney },
    { "noumea",     660,  SummerNone, StrZoneNoumea },
    { "auckland",   720,  SummerNZ,   StrZoneAuckland },
    { "saopaulo",   -180, SummerNone, StrZoneSaopaulo },
    { "martinique", -240, SummerNone, StrZoneMartinique },
    { "newyork",    -300, SummerUS,   StrZoneNewyork },
    { "chicago",    -360, SummerUS,   StrZoneChicago },
    { "denver",     -420, SummerUS,   StrZoneDenver },
    { "losangeles", -480, SummerUS,   StrZoneLosangeles },
    { "anchorage",  -540, SummerUS,   StrZoneAnchorage },
    { "honolulu",   -600, SummerNone, StrZoneHonolulu }
};
static const unsigned ZONE_COUNT = sizeof ZONES / sizeof ZONES[0];

unsigned ClockZoneCount (void)
{
    return ZONE_COUNT;
}

const char *ClockZoneKey (unsigned nZone)
{
    return nZone < ZONE_COUNT ? ZONES[nZone].pKey : "";
}

TStringId ClockZoneName (unsigned nZone)
{
    return ZONES[nZone < ZONE_COUNT ? nZone : 0].Name;
}

int ClockZoneStandard (unsigned nZone)
{
    return nZone < ZONE_COUNT ? ZONES[nZone].nStandard : 0;
}

static char Lower (char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

int ClockZoneFind (const char *pKey)
{
    if (pKey == 0)
    {
        return -1;
    }
    for (unsigned i = 0; i < ZONE_COUNT; i++)
    {
        const char *a = ZONES[i].pKey;
        const char *b = pKey;
        while (*a != '\0' && *a == Lower (*b))
        {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
        {
            return (int) i;
        }
    }
    return -1;
}

/*
 *  The calendar
 *
 *  Howard Hinnant's days_from_civil and civil_from_days, proleptic Gregorian,
 *  day 0 being 1 January 1970. Only the years a Macintosh clock can hold are
 *  ever asked for, but the arithmetic holds for all of them.
 */
static long DaysOf (long y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned) (y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long) doe - 719468;
}

static void CivilOf (long z, unsigned *pYear, unsigned *pMonth, unsigned *pDay)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = (unsigned) (z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    *pYear  = (unsigned) (yoe + era * 400 + (m <= 2));
    *pMonth = m;
    *pDay   = d;
}

static unsigned WeekdayOf (long z)
{
    return (unsigned) (z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

unsigned ClockSecondsOf (unsigned nYear, unsigned nMonth, unsigned nDay)
{
    return (unsigned) (DaysOf ((long) nYear, nMonth, nDay) * 86400L);
}

void ClockDateOf (unsigned nSeconds, TClockDate *pDate)
{
    const long nDays = (long) (nSeconds / 86400);
    const unsigned nRest = nSeconds % 86400;
    CivilOf (nDays, &pDate->nYear, &pDate->nMonth, &pDate->nDay);
    pDate->nWeekday = WeekdayOf (nDays);
    pDate->nHour    = nRest / 3600;
    pDate->nMinute  = nRest / 60 % 60;
    pDate->nSecond  = nRest % 60;
}

// The day of the nth Sunday of a month, counting from 1; 0 for the last.
static long SundayOf (unsigned nYear, unsigned nMonth, unsigned nNth)
{
    if (nNth == 0)
    {
        const long nLast = nMonth == 12 ? DaysOf ((long) nYear + 1, 1, 1) - 1
                                        : DaysOf ((long) nYear, nMonth + 1, 1) - 1;
        return nLast - (long) WeekdayOf (nLast);
    }
    const long nFirst = DaysOf ((long) nYear, nMonth, 1);
    return nFirst + (long) ((7 - WeekdayOf (nFirst)) % 7) + 7L * (long) (nNth - 1);
}

// The instant, in UTC seconds, at nMinutes past midnight local on that day,
// for a clock nOffset minutes east of UTC.
static long At (long nDay, long nMinutes, long nOffset)
{
    return nDay * 86400L + (nMinutes - nOffset) * 60L;
}

int ClockZoneOffset (unsigned nZone, unsigned nUTC)
{
    if (nZone >= ZONE_COUNT)
    {
        return 0;
    }
    const TZone *z = &ZONES[nZone];
    const long t = (long) nUTC;
    const long s = z->nStandard;
    TClockDate Date;
    ClockDateOf (nUTC, &Date);
    const unsigned y = Date.nYear;

    bool bSummer = false;
    switch (z->Rule)
    {
    case SummerEU:
        // At 01:00 UTC everywhere at once: the one rule that is not local.
        bSummer = t >= At (SundayOf (y, 3, 0), 60, 0) && t < At (SundayOf (y, 10, 0), 60, 0);
        break;

    case SummerUS:
        // 02:00 local both ways: standard time in March, summer time in November.
        bSummer = t >= At (SundayOf (y, 3, 2), 120, s) && t < At (SundayOf (y, 11, 1), 120, s + 60);
        break;

    case SummerAU:
        // Southern: summer runs across the new year, so it is winter that is
        // the interval. Back at 03:00 summer time, forward at 02:00 standard.
        bSummer = !(t >= At (SundayOf (y, 4, 1), 180, s + 60) && t < At (SundayOf (y, 10, 1), 120, s));
        break;

    case SummerNZ:
        bSummer = !(t >= At (SundayOf (y, 4, 1), 180, s + 60) && t < At (SundayOf (y, 9, 0), 120, s));
        break;

    default:
        break;
    }
    return (int) s + (bSummer ? 60 : 0);
}

void ClockOffsetText (char *pOut, unsigned nSize, int nMinutes)
{
    unsigned n = StrAppend (pOut, nSize, 0, "UTC");
    if (nMinutes == 0)
    {
        return;
    }
    n = StrAppend (pOut, nSize, n, nMinutes < 0 ? "-" : "+");
    const unsigned nAbs = (unsigned) (nMinutes < 0 ? -nMinutes : nMinutes);
    n = StrAppendNumber (pOut, nSize, n, nAbs / 60);
    if (nAbs % 60 != 0)
    {
        n = StrAppend (pOut, nSize, n, nAbs % 60 < 10 ? ":0" : ":");
        StrAppendNumber (pOut, nSize, n, nAbs % 60);
    }
}

void ClockFormat (char *pOut, unsigned nSize, unsigned nLocal)
{
    static const TStringId Days[7] =
    {
        StrDaySunday, StrDayMonday, StrDayTuesday, StrDayWednesday,
        StrDayThursday, StrDayFriday, StrDaySaturday
    };
    static const TStringId Months[12] =
    {
        StrMonthJanuary, StrMonthFebruary, StrMonthMarch, StrMonthApril,
        StrMonthMay, StrMonthJune, StrMonthJuly, StrMonthAugust,
        StrMonthSeptember, StrMonthOctober, StrMonthNovember, StrMonthDecember
    };
    TClockDate D;
    ClockDateOf (nLocal, &D);
    unsigned n = StrAppend (pOut, nSize, 0, Str (Days[D.nWeekday]));
    n = StrAppend (pOut, nSize, n, " ");
    n = StrAppendNumber (pOut, nSize, n, D.nDay);
    n = StrAppend (pOut, nSize, n, " ");
    n = StrAppend (pOut, nSize, n, Str (Months[D.nMonth - 1]));
    n = StrAppend (pOut, nSize, n, " ");
    n = StrAppendNumber (pOut, nSize, n, D.nYear);
    n = StrAppend (pOut, nSize, n, D.nHour < 10 ? ", 0" : ", ");
    n = StrAppendNumber (pOut, nSize, n, D.nHour);
    n = StrAppend (pOut, nSize, n, D.nMinute < 10 ? ":0" : ":");
    StrAppendNumber (pOut, nSize, n, D.nMinute);
}

/*
 *  The clock set on the Macintosh
 */
unsigned ClockRecordFormat (char *pOut, unsigned nSize, const TClockRecord *pRecord)
{
    unsigned n = StrAppendNumber (pOut, nSize, 0, pRecord->nSet);
    n = StrAppend (pOut, nSize, n, " ");
    n = StrAppendNumber (pOut, nSize, n, pRecord->nFloor);
    return StrAppend (pOut, nSize, n, "\n");
}

static bool Number (const char *p, unsigned nLength, unsigned *pAt, unsigned *pValue)
{
    unsigned long long nValue = 0;
    const unsigned nStart = *pAt;
    while (*pAt < nLength && p[*pAt] >= '0' && p[*pAt] <= '9')
    {
        nValue = nValue * 10 + (unsigned) (p[(*pAt)++] - '0');
        if (nValue > 0xFFFFFFFFull)
        {
            return false;
        }
    }
    *pValue = (unsigned) nValue;
    return *pAt > nStart;
}

bool ClockRecordParse (const char *pText, unsigned nLength, TClockRecord *pRecord)
{
    unsigned i = 0;
    TClockRecord R;
    if (!Number (pText, nLength, &i, &R.nSet) || i >= nLength || pText[i++] != ' ')
    {
        return false;
    }
    if (!Number (pText, nLength, &i, &R.nFloor))
    {
        return false;
    }
    // The newline is the proof the write went to the end.
    if (i >= nLength || pText[i++] != '\n' || i != nLength)
    {
        return false;
    }
    *pRecord = R;
    return true;
}

bool ClockMacPlausible (unsigned nMacSeconds)
{
    const unsigned nFirst = ClockSecondsOf (1990, 1, 1) + CLOCK_MAC_UNIX_DIFF;
    return nMacSeconds >= nFirst && nMacSeconds != 0xFFFFFFFFu;
}
