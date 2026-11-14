/*
 * clock_circle.cpp — the board's clock. See clock_circle.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_circle.h"

#include <fatfs/ff.h>
#include <string.h>

#include "sysdeps.h"
#include "prefs.h"

#include "clock_circle.h"
#include "board_net_circle.h"
#include "net_clock.h"

#define FROM "okapia-clock"

// Beside the parameter RAM files, and named like them.
static const char RECORD_PATH[] = "SD:/Okapia_Clock";

// The NTP Pool: volunteers' servers, spread over the world and meant for
// exactly this — a device that asks now and then (www.ntppool.org). Chosen
// over a company's servers on purpose: nobody's commercial service, and no
// smeared leap second to disagree with the rest.
static const char DEFAULT_SERVER[] = "pool.ntp.org";

/*
 *  The zone
 */
static int  s_nZone = 0;            // ClockZoneFind's index, or -1 for a fixed offset
static int  s_nFixed;               // minutes east of UTC, when s_nZone < 0

static unsigned s_nNextSummerCheck;

static unsigned NowMs (void)
{
    return (unsigned) (CTimer::GetClockTicks64 () / 1000);
}

static bool Due (unsigned nNow, unsigned nDeadline)
{
    return (int) (nNow - nDeadline) >= 0;
}

static int OffsetAt (unsigned nUTC)
{
    return s_nZone >= 0 ? ClockZoneOffset ((unsigned) s_nZone, nUTC) : s_nFixed;
}

// Moves the zone and keeps the instant: the local time is what changes.
// Circle keeps local seconds (timer.cpp: m_nTime), so the order matters — the
// instant is read with the old offset and written back with the new one.
static bool SetOffset (int nMinutes)
{
    CTimer *pTimer = CTimer::Get ();
    if (nMinutes == pTimer->GetTimeZone ())
    {
        return true;
    }
    const unsigned nUTC = pTimer->GetUniversalTime ();
    if (!pTimer->SetTimeZone (nMinutes))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Offset %d min refused, staying on %d",
                                nMinutes, pTimer->GetTimeZone ());
        return false;
    }
    return pTimer->SetTime (nUTC, FALSE);
}

static const char *ZoneName (char *pBuffer, unsigned nSize)
{
    pBuffer[0] = '\0';
    ClockOffsetText (pBuffer, nSize, CTimer::Get ()->GetTimeZone ());
    const unsigned n = (unsigned) strlen (pBuffer);
    StrAppend (pBuffer, nSize, n, s_nZone >= 0 ? ", " : ", fixed");
    if (s_nZone >= 0)
    {
        StrAppend (pBuffer, nSize, n + 2, ClockZoneKey ((unsigned) s_nZone));
    }
    return pBuffer;
}

static void ChooseZone (const char *pKey, int nFixed)
{
    const int nZone = ClockZoneFind (pKey);
    if (nZone >= 0)
    {
        s_nZone = nZone;
    }
    else if (nFixed != 0)
    {
        // The older preference: minutes east of UTC, with no summer time.
        if (pKey != 0 && pKey[0] != '\0')
        {
            CLogger::Get ()->Write (FROM, LogWarning, "zone %s is not one Okapia knows", pKey);
        }
        s_nZone = -1;
        s_nFixed = nFixed;
    }
    else
    {
        s_nZone = ClockZoneFind ("utc");
    }
}

void ClockInitialize (void)
{
    ChooseZone (PrefsFindString ("zone"), (int) PrefsFindInt32 ("timezone"));

    // The zone, then the time: SetTime(…, FALSE) converts with the offset the
    // timer holds, and CardRefineClock() compares in that frame.
    CTimer *pTimer = CTimer::Get ();
    if (!pTimer->SetTimeZone (OffsetAt (OKAPIA_BUILD_TIME))
        || !pTimer->SetTime (OKAPIA_BUILD_TIME, FALSE))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "The offset is out of range, staying on UTC");
        s_nZone = ClockZoneFind ("utc");
        pTimer->SetTimeZone (0);
        pTimer->SetTime (OKAPIA_BUILD_TIME, FALSE);
    }
    s_nNextSummerCheck = NowMs ();

    char Zone[40];
    CLogger::Get ()->Write (FROM, LogNotice, "Clock: %s (%s), from the build time",
                            (const char *) *pTimer->GetTimeString (),
                            ZoneName (Zone, sizeof Zone));
}

void ClockSetZone (const char *pKey, int nFixedMinutes)
{
    ChooseZone (pKey, nFixedMinutes);
    SetOffset (OffsetAt (CTimer::Get ()->GetUniversalTime ()));
    char Zone[40];
    CLogger::Get ()->Write (FROM, LogNotice, "Clock: %s (%s)",
                            (const char *) *CTimer::Get ()->GetTimeString (),
                            ZoneName (Zone, sizeof Zone));
}

/*
 *  Where the time came from
 */
static TClockSource s_Source = ClockFromBuild;
static char         s_Detail[NET_CLOCK_SERVER];
static bool         s_bSynced;
static bool         s_bDecided;

void ClockNoteSource (TClockSource Source, const char *pDetail)
{
    s_Source = Source;
    s_Detail[0] = '\0';
    if (pDetail != 0)
    {
        StrAppend (s_Detail, sizeof s_Detail, 0, pDetail);
    }
    s_bDecided = true;
}

TClockSource ClockSource (const char **ppDetail)
{
    *ppDetail = s_Detail;
    return s_Source;
}

bool ClockSynced (void)
{
    return s_bSynced;
}

bool ClockDecided (void)
{
    return s_bDecided;
}

/*
 *  The clock set on the Macintosh, on the card
 */
static TClockRecord s_Record;
static bool         s_bRecord;      // in force this power-on

static bool RecordWrite (void)
{
    char Text[32];
    const unsigned nLength = ClockRecordFormat (Text, sizeof Text, &s_Record);
    FIL File;
    if (f_open (&File, RECORD_PATH, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Cannot write %s", RECORD_PATH);
        return false;
    }
    UINT nWritten = 0;
    const FRESULT nResult = f_write (&File, Text, nLength, &nWritten);
    if (f_close (&File) != FR_OK || nResult != FR_OK || nWritten != nLength)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "%s was not written whole", RECORD_PATH);
        return false;
    }
    return true;
}

bool ClockRecordRead (TClockRecord *pRecord)
{
    FIL File;
    if (f_open (&File, RECORD_PATH, FA_READ) != FR_OK)
    {
        return false;
    }
    char Text[40];
    UINT nRead = 0;
    const FRESULT nResult = f_read (&File, Text, sizeof Text, &nRead);
    f_close (&File);
    if (nResult != FR_OK || !ClockRecordParse (Text, nRead, pRecord))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "%s is unreadable, ignored", RECORD_PATH);
        return false;
    }
    s_Record = *pRecord;
    s_bRecord = true;
    return true;
}

void ClockRecordForget (const char *pWhy)
{
    s_bRecord = false;
    // f_unlink and not remove(): see ForgetPramFile() in okapia_firmware.cpp.
    if (f_unlink (RECORD_PATH) == FR_OK)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "The clock set on the Macintosh is forgotten: %s",
                                pWhy);
    }
}

void ClockSessionEnd (void)
{
    if (!s_bRecord || s_bSynced)
    {
        return;
    }
    s_Record.nSet = CTimer::Get ()->GetLocalTime ();
    RecordWrite ();
}

/*
 *  The Macintosh's own clock
 *
 *  Time (0x20C) is what Mac OS reads for the date: GetDateTime is that global
 *  and nothing more. A real Macintosh advances it from the RTC's one-second
 *  interrupt; Basilisk II and SheepShaver emulate no such chip, so every
 *  upstream port writes it from the host clock once a second
 *  (Unix/main_unix.cpp:1295, SheepShaver Unix/main_unix.cpp:1577). Okapia did
 *  not, so the Macintosh's clock stood where the RTC read left it at startup.
 *
 *  Writing it once a second also says when the Macintosh wrote it itself:
 *  Date & Time sets this global (and the RTC, whose write upstream drops,
 *  emul_op.cpp:181). One test serves both engines, where decoding the RTC's
 *  bytes would serve the 68k one alone.
 */

// A Macintosh starting clears its low memory and reads its RTC; what it
// writes there in its first seconds is not somebody setting the clock.
static const unsigned MAC_SETTLE = 10;

static volatile bool s_bMacArmed;
static volatile unsigned s_nMacSeconds;
static volatile u32 s_nMacWritten;
static volatile bool s_bMacPending;
static volatile u32 s_nMacPending;

void ClockMacStart (void)
{
    s_bMacArmed   = false;
    s_nMacSeconds = 0;
    s_bMacPending = false;
}

bool ClockMacSecond (u32 nRead, u32 *pWrite)
{
    if (s_bMacPending)
    {
        return false;                   // ClockPoll() has not taken it yet
    }
    if (s_bMacArmed && ++s_nMacSeconds > MAC_SETTLE)
    {
        const u32 nDiff = nRead > s_nMacWritten ? nRead - s_nMacWritten
                                                : s_nMacWritten - nRead;
        // Two seconds of slack: a System that advances the global itself
        // between two of our writes is not setting anything.
        if (nDiff > 2 && ClockMacPlausible (nRead))
        {
            // What the global holds is now the reference: compared with the
            // value before it, the same setting would be found again every
            // second and the clock held on that one second for good — which is
            // what the first test under QEMU did.
            s_nMacWritten = nRead;
            s_nMacPending = nRead;
            s_bMacPending = true;
            return false;
        }
    }
    s_bMacArmed = true;
    s_nMacWritten = (u32) CTimer::Get ()->GetLocalTime () + CLOCK_MAC_UNIX_DIFF;
    *pWrite = s_nMacWritten;
    return true;
}

static void TakeMacClock (void)
{
    const u32 nMac = s_nMacPending;
    const unsigned nLocal = nMac - CLOCK_MAC_UNIX_DIFF;
    CTimer *pTimer = CTimer::Get ();
    const unsigned nWas = pTimer->GetLocalTime ();

    if (pTimer->SetTime (nLocal, TRUE))
    {
        // The latest date the card can hold so far is the clock as it was:
        // whatever the Macintosh wrote until now was dated by it.
        const unsigned nFloor = s_bRecord && s_Record.nFloor > nWas ? s_Record.nFloor : nWas;
        s_Record.nSet = nLocal;
        s_Record.nFloor = nFloor;
        s_bRecord = true;
        const bool bKept = RecordWrite ();

        const long nDelta = (long) nLocal - (long) nWas;
        ClockNoteSource (ClockFromMac, 0);
        CLogger::Get ()->Write (FROM, LogNotice, "Clock: %s, set on the Macintosh (%s%ld s)%s",
                                (const char *) *pTimer->GetTimeString (),
                                nDelta < 0 ? "" : "+", nDelta,
                                bKept ? ", kept on the card" : "");
    }
    s_bMacPending = false;
}

/*
 *  The network
 */
static TNetClock s_Net;
static char      s_Server[NET_CLOCK_SERVER];
static bool      s_bServerKnown;
static bool      s_bLinkWasUp;
static unsigned  s_nLinkUpAt;
static bool      s_bRetryAt;
static unsigned  s_nRetryAt;
static unsigned  s_nBackoff;
static u8        s_Out[NET_CLOCK_FRAME];
static u32       s_nLease;          // the address DHCP gave, kept: it is never released

// Long enough for a Wi-Fi link to finish its handshake after it says it is up.
static const unsigned LINK_SETTLE  = 3000;
static const unsigned RETRY_FIRST  = 60 * 1000;
static const unsigned RETRY_MOST   = 30 * 60 * 1000;
// A Pi's crystal drifts a second or two a day; once a day keeps it right
// without asking the pool more than its guidelines want.
static const unsigned RESYNC_AFTER = 24 * 60 * 60 * 1000;

static void LoadServer (void)
{
    if (s_bServerKnown)
    {
        return;
    }
    s_bServerKnown = true;
    const char *pServer = PrefsFindString ("ntp");
    s_Server[0] = '\0';
    if (pServer != 0)
    {
        StrAppend (s_Server, sizeof s_Server, 0, pServer);
    }
}

void ClockSetServer (const char *pServer)
{
    if (s_bServerKnown && strcmp (pServer, s_Server) == 0)
    {
        return;
    }
    s_bServerKnown = true;
    s_Server[0] = '\0';
    StrAppend (s_Server, sizeof s_Server, 0, pServer);
    // Asked again at once, by whichever name is now wanted.
    s_Net.State = NetClockIdle;
    s_bRetryAt = false;
    s_nBackoff = 0;
}

static const char *FailureText (TNetClockFailure Failure)
{
    switch (Failure)
    {
    case NetClockNoLease:  return "no DHCP server answered";
    case NetClockNoRouter: return "the router did not answer";
    case NetClockNoName:   return "the name did not resolve";
    case NetClockNoAnswer: return "no time server answered";
    default:               return "?";
    }
}

bool ClockNetLease (u32 *pAddress)
{
    *pAddress = s_nLease;
    return s_nLease != 0;
}

static void Finish (unsigned nNow)
{
    if (s_Net.nAddress != 0)
    {
        s_nLease = s_Net.nAddress;
    }
    if (s_Net.State == NetClockDone)
    {
        // The instant now: the answer's, plus what has passed since it came.
        const unsigned nUTC = s_Net.nUnix + (s_Net.nMillis + (nNow - s_Net.nAt) + 500) / 1000;
        CTimer *pTimer = CTimer::Get ();
        const long nDelta = (long) nUTC - (long) pTimer->GetUniversalTime ();
        pTimer->SetTimeZone (OffsetAt (nUTC));
        if (pTimer->SetTime (nUTC, FALSE))
        {
            s_bSynced = true;
            ClockNoteSource (ClockFromNetwork, s_Server);
            char Address[16];
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "Clock: %s, from %s (%s, stratum %u, %s%ld s)",
                                    (const char *) *pTimer->GetTimeString (), s_Server,
                                    NetClockAddress (s_Net.nFrom, Address), s_Net.nStratum,
                                    nDelta < 0 ? "" : "+", nDelta);
            if (s_bRecord)
            {
                ClockRecordForget ("the network gave the time");
            }
        }
        s_nBackoff = 0;
        s_bRetryAt = true;
        s_nRetryAt = nNow + RESYNC_AFTER;
        s_Net.State = NetClockIdle;
    }
    else if (s_Net.State == NetClockFailed)
    {
        s_nBackoff = s_nBackoff == 0 ? RETRY_FIRST
                                     : (s_nBackoff * 2 > RETRY_MOST ? RETRY_MOST : s_nBackoff * 2);
        CLogger::Get ()->Write (FROM, LogWarning, "Clock: no time from %s: %s; again in %u s",
                                s_Server, FailureText (s_Net.Failure), s_nBackoff / 1000);
        s_bRetryAt = true;
        s_nRetryAt = nNow + s_nBackoff;
        s_Net.State = NetClockIdle;
    }
}

bool ClockNetTake (const u8 *pFrame, unsigned nLength)
{
    if (!NetClockBusy (&s_Net))
    {
        return false;
    }
    const unsigned nNow = NowMs ();
    unsigned nOut = 0;
    const bool bOurs = NetClockTake (&s_Net, pFrame, nLength, nNow, s_Out, &nOut);
    if (nOut != 0)
    {
        BoardNetSend (s_Out, nOut);
    }
    Finish (nNow);
    return bOurs;
}

void ClockNetStep (bool bLinkUp)
{
    LoadServer ();
    if (s_Server[0] == '\0')
    {
        return;
    }
    const unsigned nNow = NowMs ();

    if (NetClockBusy (&s_Net))
    {
        unsigned nOut = 0;
        NetClockStep (&s_Net, nNow, s_Out, &nOut);
        if (nOut != 0)
        {
            BoardNetSend (s_Out, nOut);
        }
        Finish (nNow);
        return;
    }

    if (!bLinkUp)
    {
        s_bLinkWasUp = false;
        return;
    }
    if (!s_bLinkWasUp)
    {
        s_bLinkWasUp = true;
        s_nLinkUpAt = nNow;
    }
    if (!Due (nNow, s_nLinkUpAt + LINK_SETTLE) || (s_bRetryAt && !Due (nNow, s_nRetryAt)))
    {
        return;
    }

    const u8 *pMAC = BoardNetMAC ();
    const u32 nSeed = (u32) CTimer::GetClockTicks64 ()
                    ^ ((u32) pMAC[2] << 24 | (u32) pMAC[3] << 16 | (u32) pMAC[4] << 8 | pMAC[5]);
    NetClockBegin (&s_Net, pMAC, s_Server, nSeed, nNow);
    s_bRetryAt = false;
    CLogger::Get ()->Write (FROM, LogNotice, "Clock: asking %s", s_Server);
    Finish (nNow);                      // a name that cannot be asked fails here
}

/*
 *  The seam
 */
void ClockPoll (void)
{
    if (s_bMacPending)
    {
        TakeMacClock ();
    }

    // Summer time, looked at twice a minute: a change lands within thirty
    // seconds of the hour, which nobody reading a menu bar can tell.
    const unsigned nNow = NowMs ();
    if (Due (nNow, s_nNextSummerCheck))
    {
        s_nNextSummerCheck = nNow + 30 * 1000;
        const int nWant = OffsetAt (CTimer::Get ()->GetUniversalTime ());
        const int nHave = CTimer::Get ()->GetTimeZone ();
        if (nWant != nHave && SetOffset (nWant))
        {
            CLogger::Get ()->Write (FROM, LogNotice, "Clock: %s, %s time",
                                    (const char *) *CTimer::Get ()->GetTimeString (),
                                    nWant > nHave ? "summer" : "standard");
        }
    }
}
