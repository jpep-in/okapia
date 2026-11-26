/*
 * net_bench_circle.cpp — what the board's network can carry, with no
 * Macintosh in the way. See net_bench_circle.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_circle.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sysdeps.h"
#include "prefs.h"

#include "net_bench_circle.h"
#include "board_net_okapia.h"
#include "clock_okapia.h"
#include "net_clock.h"

#define FROM "okapia-bench"

#if !OKAPIA_NETBENCH

void NetBenchRun (void)
{
    static bool s_bSaid;
    if (s_bSaid)
    {
        return;
    }
    s_bSaid = true;
    const char *pHost = PrefsFindString ("netbench");
    if (pHost != 0 && pHost[0] != '\0')
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "netbench %s ignored: this kernel was built without the bench "
                                "(OKAPIA_NETBENCH=1)", pHost);
    }
}

#else

static const u16 PORT_SCRIPT = 9000;
static const u16 PORT_BOARD  = 9001;

// The largest UDP payload a 1500-byte frame carries: 1500 - 20 - 8.
static const unsigned PAYLOAD = 1472;

static bool     s_bRan;
static bool     s_bActive;
static u32      s_nHost;
static u32      s_nAddress;
static u8       s_HostMAC[6];
static volatile bool s_bHostKnown;

// What the script asked, set by NetBenchTake() from inside BoardNetPoll().
static volatile unsigned s_nTxSeconds;
static volatile unsigned s_nRxSeconds;
static volatile bool     s_bEnd;
static unsigned s_nEndFrames;
static volatile bool     s_bReceiving;

// What arrived while receiving.
static unsigned s_nRxFrames;
static u64      s_nRxBytes;
static u64      s_nRxFirst, s_nRxLast;  // µs
static u32      s_nRxMaxSeq;

static u8 s_Frame[1514];
static u8 s_Text[128 + 42];

static u64 Now (void)
{
    return CTimer::GetClockTicks64 ();
}

static u32 Get32 (const u8 *p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

static void Say (const char *pText)
{
    const unsigned n = (unsigned) strlen (pText);
    memcpy (s_Text + NET_UDP_PAYLOAD, pText, n);
    const unsigned nLength = NetUdpBuild (s_Text, BoardNetMAC (), s_HostMAC, s_nAddress, s_nHost,
                                          PORT_BOARD, PORT_SCRIPT, n, 0, true);
    BoardNetSend (s_Text, nLength);
}

bool NetBenchTake (const u8 *pFrame, unsigned nLength)
{
    if (!s_bActive)
    {
        return false;
    }
    u8 MAC[6];
    if (!s_bHostKnown && NetArpAnswer (pFrame, nLength, s_nHost, MAC))
    {
        memcpy (s_HostMAC, MAC, 6);
        s_bHostKnown = true;
        return false;                   // an ARP reply may be the Macintosh's business too
    }

    // Who has our address: nobody else answers before the Macintosh runs,
    // and the other machine asks before it sends.
    if (nLength >= 42 && pFrame[12] == 0x08 && pFrame[13] == 0x06 && pFrame[20] == 0
        && pFrame[21] == 1 && Get32 (pFrame + 38) == s_nAddress && s_nAddress != 0)
    {
        static u8 Reply[60];
        const unsigned n = NetArpBuild (Reply, BoardNetMAC (), s_nAddress, 2, pFrame + 22,
                                        pFrame + 22, Get32 (pFrame + 28));
        BoardNetSend (Reply, n);
        return false;
    }

    u32 nSource;
    unsigned nPayload;
    const u8 *d = NetUdpFind (pFrame, nLength, PORT_SCRIPT, PORT_BOARD, &nSource, &nPayload);
    if (d == 0 || nSource != s_nHost || nPayload < 4)
    {
        return false;
    }

    if (memcmp (d, "OKBD", 4) == 0 && nPayload >= 8)
    {
        if (s_bReceiving)
        {
            const u64 nNow = Now ();
            if (s_nRxFrames == 0)
            {
                s_nRxFirst = nNow;
            }
            s_nRxLast = nNow;
            s_nRxFrames++;
            s_nRxBytes += nPayload;
            const u32 nSeq = Get32 (d + 4);
            s_nRxMaxSeq = nSeq > s_nRxMaxSeq ? nSeq : s_nRxMaxSeq;
        }
        return true;
    }

    if (memcmp (d, "OKBN ", 5) == 0)
    {
        char Line[64];
        const unsigned n = nPayload - 5 < sizeof Line - 1 ? nPayload - 5 : sizeof Line - 1;
        memcpy (Line, d + 5, n);
        Line[n] = '\0';
        if (strncmp (Line, "TX ", 3) == 0)
        {
            s_nTxSeconds = (unsigned) atoi (Line + 3);
        }
        else if (strncmp (Line, "RX ", 3) == 0)
        {
            s_nRxSeconds = (unsigned) atoi (Line + 3);
        }
        else if (strncmp (Line, "END ", 4) == 0)
        {
            s_nEndFrames = (unsigned) atoi (Line + 4);
            s_bEnd = true;
        }
    }
    return true;
}

// Polls the interface until bDone says so or nMs pass. Answers whether it was
// bDone that ended it.
static bool PollUntil (volatile bool *pDone, unsigned nMs)
{
    const u64 nEnd = Now () + (u64) nMs * 1000;
    while (!*pDone && Now () < nEnd)
    {
        BoardNetPoll ();
    }
    return *pDone;
}

static unsigned Kbps (u64 nBytes, u64 nMicros)
{
    return nMicros == 0 ? 0 : (unsigned) (nBytes * 8 * 1000 / nMicros);
}

static void Send (unsigned nSeconds)
{
    memset (s_Frame + NET_UDP_PAYLOAD, 0, PAYLOAD);
    memcpy (s_Frame + NET_UDP_PAYLOAD, "OKBD", 4);
    const unsigned nLength = NetUdpBuild (s_Frame, BoardNetMAC (), s_HostMAC, s_nAddress, s_nHost,
                                          PORT_BOARD, PORT_SCRIPT, PAYLOAD, 0, false);
    unsigned nSent = 0, nRefused = 0;
    const u64 nStart = Now ();
    const u64 nEnd = nStart + (u64) nSeconds * 1000000;
    u64 nNow = nStart;

    // The pace, in 1/16 µs between two frames. Circle logs every frame its
    // ring refuses ("TX frame dropped", bcm54213.cpp:735) on a 115,200-baud
    // line, and sending into a full ring measured the log instead of the
    // interface: 2,611 refusals cost most of a ten-second run. So a refusal
    // spaces the frames out by an eighth more and lets the ring drain, and a
    // handful of them find the rate the interface keeps up with.
    u64 nGap16 = 0;
    u64 nNext16 = nStart * 16;
    while (nNow < nEnd)
    {
        if (nNow * 16 < nNext16)
        {
            nNow = Now ();
            continue;
        }
        u8 *pSeq = s_Frame + NET_UDP_PAYLOAD + 4;
        pSeq[0] = (u8) (nSent >> 24);
        pSeq[1] = (u8) (nSent >> 16);
        pSeq[2] = (u8) (nSent >> 8);
        pSeq[3] = (u8) nSent;
        if (BoardNetSend (s_Frame, nLength))
        {
            nSent++;
            nNext16 += nGap16;
        }
        else
        {
            nRefused++;
            nGap16 += nGap16 / 8 > 16 ? nGap16 / 8 : 16;
            CTimer::SimpleusDelay (200);
            nNext16 = Now () * 16;
        }
        // The Wi-Fi's tasks run only when this thread yields, and what comes
        // in meanwhile must still be taken off the interface.
        if (((nSent + nRefused) & 31) == 0)
        {
            BoardNetPoll ();
        }
        nNow = Now ();
    }
    const u64 nMicros = nNow - nStart;
    const u64 nBytes = (u64) nSent * PAYLOAD;

    char Done[96];
    snprintf (Done, sizeof Done, "OKBN DONE %u %llu %llu", nSent,
              (unsigned long long) nBytes, (unsigned long long) nMicros);
    for (unsigned i = 0; i < 3; i++)
    {
        Say (Done);
        CTimer::Get ()->MsDelay (50);
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Network bench: sent %u frames of %u bytes in %u ms, %u kbit/s, "
                            "%u refused by the interface, paced at last to one every %u.%02u us",
                            nSent, PAYLOAD, (unsigned) (nMicros / 1000),
                            Kbps (nBytes, nMicros), nRefused,
                            (unsigned) (nGap16 / 16), (unsigned) (nGap16 % 16 * 100 / 16));
}

static void Receive (unsigned nSeconds)
{
    s_nRxFrames = 0;
    s_nRxBytes = 0;
    s_nRxMaxSeq = 0;
    s_bEnd = false;
    s_bReceiving = true;
    PollUntil (&s_bEnd, (nSeconds + 5) * 1000);
    s_bReceiving = false;

    const u64 nMicros = s_nRxLast > s_nRxFirst ? s_nRxLast - s_nRxFirst : 0;
    const unsigned nSent = s_bEnd ? s_nEndFrames : s_nRxMaxSeq + 1;
    char Result[96];
    snprintf (Result, sizeof Result, "OKBN RESULT %u %llu %llu", s_nRxFrames,
              (unsigned long long) s_nRxBytes, (unsigned long long) nMicros);
    for (unsigned i = 0; i < 3; i++)
    {
        Say (Result);
        CTimer::Get ()->MsDelay (50);
    }
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Network bench: received %u of %u frames in %u ms, %u kbit/s, "
                            "%u lost",
                            s_nRxFrames, nSent, (unsigned) (nMicros / 1000),
                            Kbps (s_nRxBytes, nMicros),
                            nSent > s_nRxFrames ? nSent - s_nRxFrames : 0);
}

void NetBenchRun (void)
{
    if (s_bRan)
    {
        return;
    }
    s_bRan = true;

    const char *pHost = PrefsFindString ("netbench");
    if (pHost == 0 || pHost[0] == '\0')
    {
        return;
    }
    // "<other machine> [<this board>]": the second address when nothing on
    // the cable serves DHCP — a Mac's adapter set by hand, without Internet
    // Sharing, which only works if it is switched on after the link is up.
    char Host[20], Own[20];
    Host[0] = Own[0] = '\0';
    sscanf (pHost, "%19s %19s", Host, Own);
    if (!NetParseAddress (Host, &s_nHost))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "netbench %s is not an IPv4 address", Host);
        return;
    }
    if (Own[0] != '\0' && !NetParseAddress (Own, &s_nAddress))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "netbench: %s is not an IPv4 address", Own);
        return;
    }
    pHost = Host;
    CLogger::Get ()->Write (FROM, LogNotice, "Network bench: against %s, before the Macintosh",
                            pHost);
    if (!BoardNetStart ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Network bench: no network to measure");
        return;
    }
    s_bActive = true;

    // The address: the clock's lease, which the clock asks for on its own a
    // few seconds after the link comes up (`ntp` must not be empty).
    const u64 nGiveUp = Now () + 20 * 1000000ull;
    while (s_nAddress == 0 && !ClockNetLease (&s_nAddress) && Now () < nGiveUp)
    {
        BoardNetPoll ();
    }
    // A fixed address still waits for the link, which the ARP below needs.
    // Asked ten times a second: each question reconfigures the MAC from the PHY.
    const u64 nLinkBy = Now () + 15 * 1000000ull;
    u64 nNextAsk = 0;
    bool bLink = false;
    while (!bLink && Now () < nLinkBy)
    {
        BoardNetPoll ();
        if (Now () >= nNextAsk)
        {
            nNextAsk = Now () + 100000;
            bLink = BoardNetLinkUp (0);
        }
    }
    if (s_nAddress == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "Network bench: no address after 20 s (the bench takes the "
                                "clock's DHCP lease, so `ntp` must name a server)");
        s_bActive = false;
        return;
    }

    // The other machine's MAC.
    static const u8 Broadcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const u8 Zero[6] = { 0, 0, 0, 0, 0, 0 };
    // Two seconds, not ten: the bench holds up the start, and a card that
    // still names a machine no longer on the cable must not cost every boot.
    for (unsigned i = 0; i < 4 && !s_bHostKnown; i++)
    {
        const unsigned n = NetArpBuild (s_Frame, BoardNetMAC (), s_nAddress, 1, Broadcast, Zero,
                                        s_nHost);
        BoardNetSend (s_Frame, n);
        PollUntil (&s_bHostKnown, 500);
    }
    if (!s_bHostKnown)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Network bench: %s does not answer ARP", pHost);
        s_bActive = false;
        return;
    }

    // The script, when it is ready: two minutes to start it.
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Network bench: waiting for scripts/netbench.py on %s", pHost);
    volatile bool bAsked = false;
    for (unsigned i = 0; i < 120 && !bAsked; i++)
    {
        Say ("OKBN HELLO");
        const u64 nEnd = Now () + 1000000;
        while (s_nTxSeconds == 0 && Now () < nEnd)
        {
            BoardNetPoll ();
        }
        bAsked = s_nTxSeconds != 0;
    }
    if (!bAsked)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Network bench: nobody asked; skipped");
        s_bActive = false;
        return;
    }

    Send (s_nTxSeconds < 60 ? s_nTxSeconds : 60);

    const u64 nWait = Now () + 10 * 1000000ull;
    while (s_nRxSeconds == 0 && Now () < nWait)
    {
        BoardNetPoll ();
    }
    if (s_nRxSeconds != 0)
    {
        Receive (s_nRxSeconds < 60 ? s_nRxSeconds : 60);
    }
    s_bActive = false;
    CLogger::Get ()->Write (FROM, LogNotice, "Network bench: done");
}

#endif  // OKAPIA_NETBENCH
