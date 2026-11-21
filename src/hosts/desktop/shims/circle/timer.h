// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// CTimer, as the platform files use it, on the host's monotonic clock and its
// time of day. Defined in timer_desktop.cpp.
#ifndef _circle_timer_h
#define _circle_timer_h
#include <circle/types.h>

#define HZ      100
#define CLOCKHZ 1000000

class CDesktopTimeString
{
public:
    operator const char * (void) const { return m_Text; }
    char m_Text[40];
};

typedef void TPeriodicTimerHandler (void);

class CTimer
{
public:
    static CTimer *Get (void);
    static unsigned GetClockTicks (void);       // microseconds, wrapping
    static u64 GetClockTicks64 (void);
    static void SimpleusDelay (unsigned nMicros);
    static void SimpleMsDelay (unsigned nMs);
    unsigned GetTicks (void);                   // HZ ticks since start
    unsigned GetTime (void);                    // UTC seconds since 1970
    unsigned GetLocalTime (void);               // local seconds since 1970
    int GetTimeZone (void);                     // minutes
    bool SetTime (unsigned nTime, bool bLocal = true);
    CDesktopTimeString *GetTimeString (void);
    void MsDelay (unsigned nMs);
    void usDelay (unsigned nMicros);
    void RegisterPeriodicHandler (TPeriodicTimerHandler *pHandler);
};
#endif
