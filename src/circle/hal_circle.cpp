/*
 * hal_circle.cpp — bringing a Raspberry Pi up, once, in the right order.
 *
 * Every comment here was paid for by a failure, and they are gathered rather
 * than repeated: see hal_circle.h for what this owns and what it deliberately
 * does not.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal_circle.h"

#include <circle/logger.h>
#include <circle/memory.h>
#include <circle_glue.h>

#ifndef OKAPIA_BUILD_TIME
#define OKAPIA_BUILD_TIME 0
#endif

COkapiaBoard::COkapiaBoard (void)
:   m_bStarted (false), m_bConsole (false), m_bUSB (false), m_bCard (false),
    m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_Console (&m_Serial, &m_Serial),    // both non-null: see AGENTS.md
    m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
    m_pUSBHCI (0)
{
    m_ActLED.Blink (2);
}

COkapiaBoard::~COkapiaBoard (void)
{
    // Nothing is given back. A Raspberry Pi that has stopped needing its serial
    // port has stopped, and the one thing a destructor could still do is hide
    // the reason.
}

bool COkapiaBoard::Start (const char *pName)
{
    if (m_bStarted)
    {
        return true;
    }
    // Serial first, always. A failure before this point cannot be told from a
    // hang, because there is nothing yet that could say so.
    if (!m_Serial.Initialize (115200))
    {
        return false;
    }
    m_bStarted = true;
    if (!m_Logger.Initialize (&m_Serial))
    {
        return false;
    }

    m_Logger.Write (pName, LogNotice, "Okapia, built " __DATE__ " " __TIME__);

    if (!m_Interrupt.Initialize () || !m_Timer.Initialize ())
    {
        m_Logger.Write (pName, LogError, "Interrupt or timer failed");
        return false;
    }

    // The one place the clock is set. Everything that asks the time afterwards
    // reads Circle's — the Mac through TimerDateTime(), FatFs through
    // get_fattime() — so there is a single source and no second opinion.
    //
    // That source is the build time, because there is no other yet: a Pi has no
    // RTC and NTP needs the network (phase 10). Left at zero, Circle counts
    // from 1970 and FatFs stamps every file 1980-00-00, which is not even a
    // valid date; the Mac stamps volumes with 1904 dates, leaving drLsMod
    // earlier than drCrDate and an MDB fsck_hfs calls damaged. So the clock is
    // wrong by however long ago this was built, but it is ordered and
    // plausible, which is what file systems care about.
    if (!m_Timer.SetTime (OKAPIA_BUILD_TIME, FALSE))
    {
        m_Logger.Write (pName, LogWarning,
                        "Could not set the clock; dates will start at 1970");
    }

    return true;
}

bool COkapiaBoard::StartConsole (void)
{
    if (m_bConsole)
    {
        return true;
    }
    // The console goes on the serial port: the screen belongs to the Macintosh.
    // Before any file is opened, because CGlueStdioInit is what claims file
    // descriptors 0 to 2 — open a file first and it lands on stdin's slot.
    if (!m_Console.Initialize ())
    {
        return false;
    }
    CGlueStdioInit (m_Console);
    m_bConsole = true;
    return true;
}

bool COkapiaBoard::StartUSB (void)
{
    if (m_bUSB)
    {
        return true;
    }
    m_pUSBHCI = new CUSBHCIDevice (&m_Interrupt, &m_Timer, TRUE);
    if (m_pUSBHCI == 0 || !m_pUSBHCI->Initialize ())
    {
        return false;
    }
    m_bUSB = true;
    return true;
}

bool COkapiaBoard::StartCard (void)
{
    if (m_bCard)
    {
        return true;
    }
    if (!m_EMMC.Initialize ())
    {
        return false;
    }
    // "SD:" is the name the rest of Okapia opens files under, and the 1 asks
    // FatFs to mount now rather than on first access — so a card that cannot be
    // read says so here instead of from inside something else.
    m_bCard = f_mount (&m_FileSystem, "SD:", 1) == FR_OK;
    return m_bCard;
}

/*
 *  The one fine timer. See hal_circle.h for why it lives on this side.
 */
#if RASPPI <= 4

#include <circle/usertimer.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

static CUserTimer          *s_pFineTimer;
static TBoardTickHandler   *s_pTickHandler;
static unsigned             s_nPeriodUsec = 16625;
static unsigned             s_nNextDue;

// Never wake sooner than this: CUserTimer::Start asserts on a delay of 1 or
// less, and a deadline already past has to become a fresh one rather than a
// storm of immediate interrupts.
static const unsigned MIN_DELAY_USEC = 200;

static void FineTickHandler (CUserTimer *pTimer, void *pParam)
{
    (void) pParam;

    // Rearmed before the work, so a slow tick delays this one and not the next.
    const unsigned nNow = CTimer::GetClockTicks ();
    s_nNextDue += s_nPeriodUsec;
    int nDelay = (int) (s_nNextDue - nNow);
    if (nDelay < (int) MIN_DELAY_USEC)
    {
        // Late. Resynchronise, never repay: upstream's own thread has the same
        // rule (main_unix.cpp:1348), and repaying turned one delay into sixteen
        // ticks at 0 ms followed by a 140 ms hole.
        s_nNextDue = nNow + s_nPeriodUsec;
        nDelay = (int) s_nPeriodUsec;
    }
    pTimer->Start ((unsigned) nDelay);

    TBoardTickHandler *pHandler = s_pTickHandler;
    if (pHandler != 0)
    {
        (*pHandler) ();
    }
}

bool BoardFineTick (unsigned nPeriodUsec, TBoardTickHandler *pHandler)
{
    s_nPeriodUsec  = nPeriodUsec;
    s_pTickHandler = pHandler;

    if (s_pFineTimer != 0)
    {
        // The second Macintosh of a session arrives here. Said once, because
        // this is the line that will show the engine switch was survived: the
        // claim before it connected ARM_IRQ_TIMER1, and interrupt.cpp:145
        // asserts on a second connection — which halts the board.
        static bool s_bSaid;
        if (!s_bSaid)
        {
            s_bSaid = true;
            CLogger::Get ()->Write ("okapia-board", LogNotice,
                                    "Fine timer already claimed; handler swapped "
                                    "for the other engine");
        }
        return true;
    }

    s_pFineTimer = new CUserTimer (CInterruptSystem::Get (), FineTickHandler);
    if (s_pFineTimer == 0 || !s_pFineTimer->Initialize ())
    {
        delete s_pFineTimer;
        s_pFineTimer = 0;
        s_pTickHandler = 0;
        return false;
    }
    s_nNextDue = CTimer::GetClockTicks () + nPeriodUsec;
    s_pFineTimer->Start (nPeriodUsec);
    return true;
}

#else

// A Pi 5 is not assumed to have it: nothing here reaches for that board's
// hardware without having seen it work.
bool BoardFineTick (unsigned, TBoardTickHandler *)
{
    return false;
}

#endif

COkapiaBoard &OkapiaBoard (void)
{
    // Function-local, so it is constructed the first time somebody asks and not
    // before Circle's startup has run. Never destroyed: see the destructor.
    static COkapiaBoard s_Board;
    return s_Board;
}
