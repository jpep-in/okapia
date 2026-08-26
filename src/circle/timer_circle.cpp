/*
 * timer_circle.cpp — Okapia platform layer: time for the emulated Mac.
 *
 * Built on Circle's timer rather than POSIX. circle-newlib does implement
 * clock_gettime, but its <time.h> never declares it in this configuration
 * (_POSIX_TIMERS stays undefined), and going through Circle directly is shorter
 * than working around that.
 *
 * Derived in shape from BasiliskII/src/Unix/timer_unix.cpp (Christian Bauer
 * et al.); the time source and the epoch handling are ours.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include "macos_util.h"
#include "timer.h"

#define FROM "okapia"

/*
 *  The Mac counts seconds from 1 January 1904, Unix from 1 January 1970.
 *  The difference is 66 years including 17 leap days.
 */
static const uint32 MAC_UNIX_EPOCH_DIFF = 2082844800U;

/*
 *  Current date and time, in Mac seconds.
 *
 *  This is the single point where the guest learns what time it is. The plan's
 *  order of trust — NTP, then RTC, then the value kept on the card — is applied
 *  by the kernel before the Mac starts; by the time this is called, Circle's
 *  clock already holds the best answer available.
 */

uint32 TimerDateTime (void)
{
    unsigned nUnixSeconds = CTimer::Get ()->GetTime ();
    if (nUnixSeconds == 0)
    {
        // Circle has no time set: report the Mac epoch rather than 1970, so a
        // wrong clock looks obviously wrong instead of plausibly wrong.
        return 0;
    }
    return (uint32) nUnixSeconds + MAC_UNIX_EPOCH_DIFF;
}

/*
 *  Microseconds since boot, as a 64-bit value split in two.
 *  Circle's clock counter is already microseconds (CLOCKHZ == 1000000).
 */

void Microseconds (uint32 &hi, uint32 &lo)
{
    u64 nTicks = CTimer::GetClockTicks64 ();
    hi = (uint32) (nTicks >> 32);
    lo = (uint32) nTicks;
}

/*
 *  Internal time arithmetic. tm_time_t is struct timespec here, matching the
 *  Unix layer, so the Mac-side conversions below stay identical to upstream's.
 */

void timer_current_time (tm_time_t &t)
{
    u64 nMicros = CTimer::GetClockTicks64 ();
    t.tv_sec  = (time_t) (nMicros / 1000000);
    t.tv_nsec = (long) ((nMicros % 1000000) * 1000);
}

void timer_add_time (tm_time_t &res, tm_time_t a, tm_time_t b)
{
    res.tv_sec  = a.tv_sec + b.tv_sec;
    res.tv_nsec = a.tv_nsec + b.tv_nsec;
    if (res.tv_nsec >= 1000000000)
    {
        res.tv_sec++;
        res.tv_nsec -= 1000000000;
    }
}

void timer_sub_time (tm_time_t &res, tm_time_t a, tm_time_t b)
{
    res.tv_sec  = a.tv_sec - b.tv_sec;
    res.tv_nsec = a.tv_nsec - b.tv_nsec;
    if (res.tv_nsec < 0)
    {
        res.tv_sec--;
        res.tv_nsec += 1000000000;
    }
}

int timer_cmp_time (tm_time_t a, tm_time_t b)
{
    if (a.tv_sec  != b.tv_sec)  return a.tv_sec  > b.tv_sec  ? 1 : -1;
    if (a.tv_nsec != b.tv_nsec) return a.tv_nsec > b.tv_nsec ? 1 : -1;
    return 0;
}

/*
 *  Mac time values are signed: positive means milliseconds, negative means
 *  negated microseconds. Same convention as upstream.
 */

void timer_mac2host_time (tm_time_t &res, int32 mactime)
{
    if (mactime > 0)
    {
        res.tv_sec  = mactime / 1000;
        res.tv_nsec = (mactime % 1000) * 1000000;
    }
    else
    {
        res.tv_sec  = -mactime / 1000000;
        res.tv_nsec = (-mactime % 1000000) * 1000;
    }
}

int32 timer_host2mac_time (tm_time_t hosttime)
{
    if (hosttime.tv_sec < 0)
    {
        return 0;
    }

    uint64 t = (uint64) hosttime.tv_sec * 1000000 + hosttime.tv_nsec / 1000;
    if (t > 0x7fffffff)
    {
        return (int32) (t / 1000);      // too large for microseconds: milliseconds
    }
    return -(int32) t;                  // negative means microseconds
}
