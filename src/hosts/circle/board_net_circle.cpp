/*
 * board_net_circle.cpp — the board's network interface, claimed once.
 *
 * See board_net_okapia.h. In SHARED_SRCS (src/kernel/Makefile): both engines'
 * ether_okapia.cpp call into this one copy, and the device, the ring and the
 * scheduler exist once for the life of the board.
 *
 * Like board_sound_circle.cpp, this reads the Macintosh's preferences, so it
 * names prefs.h; the theme specimen kernel does not link it.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "board_net_okapia.h"
#include "wlan_circle.h"

#include <circle/logger.h>
#include <circle/macaddress.h>
#include <circle/netdevice.h>
#include <circle/string.h>
#include <circle/timer.h>
#if RASPPI == 4
#include <circle/bcm54213.h>
#endif

#include <string.h>

#include "net_config_okapia.h"
#include "clock_okapia.h"
#include "net_bench_circle.h"

#include "sysdeps.h"
#include "prefs.h"

#define FROM "okapia-net"

static bool          s_bTried;
static bool          s_bStarted;
static TBoardNetKind s_Kind = BoardNetNone;
static CNetDevice   *s_pDevice;
static u8            s_MAC[6];
static bool          s_bListening;

/*
 *  The ring, from the interface to the Macintosh
 *
 *  Both ends run on the emulation thread: BoardNetPoll() fills it from the
 *  seam, the engine's Ethernet interrupt empties it. Allocated once, at the
 *  first start, and never again — 64 frames is about 6 ms of a saturated
 *  100 Mbit/s link, and a Macintosh that does not drain that fast loses frames
 *  exactly as a real one does when its receive buffers are full.
 */

static const unsigned RING_SLOTS = 64;
static const unsigned MAX_FRAME  = 1514;        // what the Mac's drivers accept, no FCS

struct TRingSlot
{
    unsigned nLength;
    u8       Frame[MAX_FRAME];
};

static TRingSlot *s_pRing;
static unsigned   s_nIn, s_nOut;
static u8         s_RxBuffer[FRAME_BUFFER_SIZE];

// What did not reach the Macintosh, and why. Reported when it changes.
static unsigned s_nDroppedFull, s_nDroppedLong, s_nReported;

// Traffic, for the periodic report: frames off the interface, how many went to
// the Macintosh and how many to the supplicant, frames sent and refused.
static unsigned s_nRxFrames, s_nRxToMac, s_nRxEAPOL, s_nRxClock, s_nTxFrames, s_nTxFailed;

// prefs_okapia.cpp: whether the five-second reports go out.
extern bool PerfReportWanted (void);

/*
 *  Multicast groups
 *
 *  Circle takes the whole list at once, terminated by a zero address. Fifteen
 *  fits the Pi 4's own filter (seventeen entries, two taken by the broadcast
 *  and the interface's address); AppleTalk asks for one, mDNS for one.
 */

static const unsigned MAX_GROUPS = 15;
static u8       s_Groups[MAX_GROUPS + 1][MAC_ADDRESS_SIZE];
static unsigned s_nGroups;

static void GroupsApply (void)
{
    memset (s_Groups[s_nGroups], 0, MAC_ADDRESS_SIZE);
    if (s_pDevice != 0)
    {
        s_pDevice->SetMulticastFilter (s_Groups);
    }
}

bool BoardNetJoinGroup (const u8 *pAddress)
{
    if (!s_bStarted)
    {
        return false;
    }
    for (unsigned i = 0; i < s_nGroups; i++)
    {
        if (memcmp (s_Groups[i], pAddress, MAC_ADDRESS_SIZE) == 0)
        {
            return true;
        }
    }
    if (s_nGroups >= MAX_GROUPS)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Multicast list full (%u groups)", MAX_GROUPS);
        return false;
    }
    memcpy (s_Groups[s_nGroups++], pAddress, MAC_ADDRESS_SIZE);
    GroupsApply ();
    return true;
}

bool BoardNetLeaveGroup (const u8 *pAddress)
{
    for (unsigned i = 0; i < s_nGroups; i++)
    {
        if (memcmp (s_Groups[i], pAddress, MAC_ADDRESS_SIZE) == 0)
        {
            memmove (s_Groups[i], s_Groups[i + 1], (s_nGroups - i - 1) * MAC_ADDRESS_SIZE);
            s_nGroups--;
            GroupsApply ();
            return true;
        }
    }
    return false;
}

/*
 *  Starting
 */

static CNetDevice *StartWired (void)
{
#if RASPPI == 4
    // The Pi 4's own port. Polled, no interrupt: nothing to claim but the
    // device itself, and Circle cannot give that back either.
    CBcm54213Device *pGENET = new CBcm54213Device;
    if (pGENET != 0 && pGENET->Initialize ())
    {
        pGENET->UpdatePHY ();
        return pGENET;
    }
    CLogger::Get ()->Write (FROM, LogWarning, "The Pi 4's Ethernet port did not start");
#endif
    // A USB adapter, or QEMU's usb-net. USB is attached before the Macintosh
    // starts and not again (kernel.cpp), so it is there now or not at all.
    CNetDevice *pDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet);
    if (pDevice != 0)
    {
        // Since Circle 51.1, SMSC951x and LAN7800 receive nothing until a call
        // to UpdatePHY() has seen the link: without this one, the first two
        // seconds would be deaf and the log would report a link down that is
        // not. QEMU's usb-net keeps the base class's, which does nothing.
        pDevice->UpdatePHY ();
    }
    return pDevice;
}

static CNetDevice *StartWireless (void)
{
    char SSID[64], Password[80], Country[8];
    NetCopyValue (SSID, sizeof SSID, PrefsFindString ("wifissid"));
    NetCopyValue (Password, sizeof Password, PrefsFindString ("wifipassword"));
    NetCopyValue (Country, sizeof Country, PrefsFindString ("wificountry"));

    // Capitals, and two of them: the supplicant compares with its own table.
    for (char *p = Country; *p != '\0'; p++)
    {
        if (*p >= 'a' && *p <= 'z')
        {
            *p = (char) (*p - 'a' + 'A');
        }
    }
    if (Country[0] == '\0')
    {
        // The driver will not join without one. The United States' is the
        // narrowest common answer — channels 1 to 11, which every router
        // defaults to — and the log says how to do better.
        strcpy (Country, "US");
        CLogger::Get ()->Write (FROM, LogWarning,
                                "wificountry not set: using US, channels 12 and 13 unavailable");
    }

    CNetDevice *pDevice = WlanStart (SSID, Password, Country);
    memset (Password, 0, sizeof Password);
    return pDevice;
}

bool BoardNetStart (void)
{
    if (s_bTried)
    {
        return s_bStarted;
    }

    CLogger *pLog = CLogger::Get ();

    // `ether` is the switch, and `nonet` is not consulted. Every card written
    // before the network existed carries "nonet true": the default was forced
    // and SavePrefs() wrote it out. Honouring it would make "ether wlan" do
    // nothing on exactly the cards people already have.
    const char *pEther = PrefsFindString ("ether");
    const TNetEtherKind Kind = NetEtherKindOf (pEther);
    if (Kind != NetEtherNone && PrefsFindBool ("nonet"))
    {
        pLog->Write (FROM, LogNotice, "nonet is ignored: ether decides");
    }
    if (Kind == NetEtherNone)
    {
        if (pEther != 0 && pEther[0] != '\0')
        {
            pLog->Write (FROM, LogWarning, "No network: ether is \"%s\", not eth or wlan", pEther);
        }
        else
        {
            pLog->Write (FROM, LogNotice, "No network: ether is not set");
        }
        return false;
    }

    // Only now is the attempt spent. "No network" is not one: the boot menu
    // may choose a connection after the Macintosh has run without any, and
    // asking again then has to start it. A device that failed is final —
    // Circle cannot give one back to try again.
    s_bTried = true;

    // Before the device: a ring that cannot be had is a reason not to claim one.
    s_pRing = new TRingSlot[RING_SLOTS];
    if (s_pRing == 0)
    {
        pLog->Write (FROM, LogError, "No network: no memory for the receive ring");
        return false;
    }

    s_pDevice = Kind == NetEtherWired ? StartWired () : StartWireless ();
    if (s_pDevice == 0)
    {
        pLog->Write (FROM, LogError, "No network: %s",
                     Kind == NetEtherWired ? "no Ethernet interface found"
                                           : "Wi-Fi did not start");
        return false;
    }

    s_Kind = Kind == NetEtherWired ? BoardNetWired : BoardNetWLAN;
    s_pDevice->GetMACAddress ()->CopyTo (s_MAC);
    s_bStarted = true;

    CString MAC;
    s_pDevice->GetMACAddress ()->Format (&MAC);
    pLog->Write (FROM, LogNotice, "Network: %s, the Macintosh is %s",
                 s_Kind == BoardNetWired ? "Ethernet" : "Wi-Fi", (const char *) MAC);
    return true;
}

TBoardNetKind BoardNetKind (void)
{
    return s_Kind;
}

bool BoardNetTried (void)
{
    return s_bTried;
}

bool BoardNetLinkUp (unsigned *pMbps)
{
    if (pMbps != 0)
    {
        *pMbps = 0;
    }
    if (!s_bStarted)
    {
        return false;
    }
    // The PHY is what learns whether a cable is there; the boot menu asks about
    // once a second, which is what Circle's own CPHYTask does.
    if (s_Kind == BoardNetWired)
    {
        s_pDevice->UpdatePHY ();
    }
    const bool bUp = s_pDevice->IsLinkUp ();
    if (bUp && pMbps != 0)
    {
        switch (s_pDevice->GetLinkSpeed ())
        {
        case NetDeviceSpeed10Half:   case NetDeviceSpeed10Full:   *pMbps = 10;   break;
        case NetDeviceSpeed100Half:  case NetDeviceSpeed100Full:  *pMbps = 100;  break;
        case NetDeviceSpeed1000Half: case NetDeviceSpeed1000Full: *pMbps = 1000; break;
        default:                                                  break;
        }
    }
    return bUp;
}

const u8 *BoardNetMAC (void)
{
    return s_MAC;
}

void BoardNetListen (bool bListen)
{
    s_bListening = bListen && s_bStarted;
    s_nIn = s_nOut = 0;
    if (s_nGroups != 0)
    {
        s_nGroups = 0;
        GroupsApply ();
    }
}

/*
 *  Polling
 */

// What one visit may take off the interface. The ring holds more; this only
// bounds how long a burst can hold up the Macintosh's instruction stream.
static const unsigned FRAMES_PER_POLL = 16;

// One look at the link a second, by the clock and not by the number of
// visits: the boot menu visits a hundred times a second, the engine's seam a
// thousand, and the network bench's loop a hundred thousand — where a count of
// visits made the five-second report a line every few milliseconds and asked
// the PHY to reconfigure the MAC a hundred times a second.
static const unsigned LINK_EVERY_USEC = 1000000;

// What the last look at the link found, for the clock's question: asking the
// device a thousand times a second costs a register read over MDIO each time.
static bool s_bLinkUp;

static void WatchLink (void)
{
    static unsigned s_nLooks;
    static unsigned s_nLastLook;
    static bool     s_bLinkKnown;

    const unsigned nNow = CTimer::GetClockTicks ();
    if (s_nLooks != 0 && nNow - s_nLastLook < LINK_EVERY_USEC)
    {
        return;
    }
    s_nLastLook = nNow;
    ++s_nLooks;

    // The PHY is what learns the speed the cable negotiated, and the MAC must
    // be told it; Circle's own CPHYTask does this every two seconds.
    if (s_Kind == BoardNetWired && s_nLooks % 2 == 0)
    {
        s_pDevice->UpdatePHY ();
    }

    const bool bUp = s_pDevice->IsLinkUp ();
    if (!s_bLinkKnown || bUp != s_bLinkUp)
    {
        s_bLinkKnown = true;
        s_bLinkUp = bUp;
        if (s_Kind == BoardNetWLAN)
        {
            CLogger::Get ()->Write (FROM, LogNotice, bUp ? "Wi-Fi: connected to \"%s\""
                                                         : "Wi-Fi: not connected to \"%s\"",
                                    WlanSSID ());
        }
        else
        {
            CLogger::Get ()->Write (FROM, LogNotice, bUp ? "Ethernet: link up (%s)"
                                                         : "Ethernet: link down%s",
                                    bUp ? CNetDevice::GetSpeedString (s_pDevice->GetLinkSpeed ())
                                        : "");
        }
    }

    // With the other counters every five seconds: the transitions above are
    // said once, at a moment a serial cable plugged in later never sees.
    if (s_nLooks % 5 == 0 && PerfReportWanted ())
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "%s %s, Mac %s: rx %u (%u to the Mac, %u EAPOL, %u clock), "
                                "tx %u (%u refused), %u groups",
                                s_Kind == BoardNetWLAN ? "Wi-Fi" : "Ethernet",
                                bUp ? "up" : "down",
                                s_bListening ? "listening" : "not listening",
                                s_nRxFrames, s_nRxToMac, s_nRxEAPOL, s_nRxClock,
                                s_nTxFrames, s_nTxFailed, s_nGroups);
    }

    // Every sixteen seconds at most: a Macintosh still starting drains nothing,
    // and a line a second would bury the rest of its boot.
    const unsigned nDropped = s_nDroppedFull + s_nDroppedLong;
    if (nDropped != s_nReported && s_nLooks % 16 == 0)
    {
        s_nReported = nDropped;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "Frames not delivered: %u with the ring full, %u too long",
                                s_nDroppedFull, s_nDroppedLong);
    }
}

void BoardNetPoll (void)
{
    if (!s_bStarted)
    {
        return;
    }

    if (s_Kind == BoardNetWLAN)
    {
        WlanYield ();
    }

    for (unsigned i = 0; i < FRAMES_PER_POLL; i++)
    {
        unsigned nLength = 0;
        if (!s_pDevice->ReceiveFrame (s_RxBuffer, &nLength))
        {
            break;
        }
        s_nRxFrames++;
        if (nLength < 14)
        {
            continue;
        }
        const u16 nType = (u16) (s_RxBuffer[12] << 8 | s_RxBuffer[13]);
        if (s_Kind == BoardNetWLAN && nType == WLAN_ETHERTYPE_EAPOL)
        {
            s_nRxEAPOL++;
            WlanTakeEAPOL (s_RxBuffer, nLength);
            continue;
        }
        // The clock's question to the network, when one is out: its answers
        // carry a transaction number or a port of its own (net_clock.h).
        if (ClockNetTake (s_RxBuffer, nLength))
        {
            s_nRxClock++;
            continue;
        }
        if (NetBenchTake (s_RxBuffer, nLength))
        {
            continue;
        }
        if (!s_bListening)
        {
            continue;           // a closed driver hears nothing
        }
        if (nLength > MAX_FRAME)
        {
            s_nDroppedLong++;
            continue;
        }
        if (s_nIn - s_nOut >= RING_SLOTS)
        {
            s_nDroppedFull++;
            continue;
        }
        TRingSlot *pSlot = &s_pRing[s_nIn % RING_SLOTS];
        memcpy (pSlot->Frame, s_RxBuffer, nLength);
        pSlot->nLength = nLength;
        s_nIn++;
        s_nRxToMac++;
    }

    WatchLink ();
    ClockNetStep (s_bLinkUp);
}

bool BoardNetPending (void)
{
    return s_nIn != s_nOut;
}

void BoardNetDiscard (void)
{
    s_nOut = s_nIn;
}

unsigned BoardNetReceive (u8 *pBuffer)
{
    if (s_nIn == s_nOut)
    {
        return 0;
    }
    const TRingSlot *pSlot = &s_pRing[s_nOut % RING_SLOTS];
    const unsigned nLength = pSlot->nLength;
    memcpy (pBuffer, pSlot->Frame, nLength);
    s_nOut++;
    return nLength;
}

bool BoardNetInject (const u8 *pFrame, unsigned nLength)
{
    if (!s_bListening || nLength < 14 || nLength > MAX_FRAME || s_nIn - s_nOut >= RING_SLOTS)
    {
        return false;
    }
    TRingSlot *pSlot = &s_pRing[s_nIn % RING_SLOTS];
    memcpy (pSlot->Frame, pFrame, nLength);
    pSlot->nLength = nLength;
    s_nIn++;
    return true;
}

bool BoardNetSend (const u8 *pFrame, unsigned nLength)
{
    if (!s_bStarted || nLength < 14 || nLength > MAX_FRAME)
    {
        return false;
    }
    // 60 bytes is Ethernet's minimum before the FCS. A Macintosh sends ARP and
    // AppleTalk frames shorter than that and leaves the padding to its card.
    static u8 Padded[60];
    if (nLength < sizeof Padded)
    {
        memcpy (Padded, pFrame, nLength);
        memset (Padded + nLength, 0, sizeof Padded - nLength);
        pFrame = Padded;
        nLength = sizeof Padded;
    }
    s_nTxFrames++;
    if (!s_pDevice->SendFrame (pFrame, nLength))
    {
        s_nTxFailed++;
        return false;
    }
    return true;
}
