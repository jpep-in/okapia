/*
 * ether_circle.cpp — the Macintosh's Ethernet, on the board's interface.
 *
 * Replaces dummy/ether_dummy.cpp in both engines. The Macintosh shares the
 * Pi's MAC address and sees the local network as it is (docs/topics/network.md):
 * its frames go out through BoardNetSend() unchanged, and every frame the
 * interface receives comes back to it, from its own Ethernet interrupt.
 *
 * Adapted from Basilisk II's Unix/ether_unix.cpp (Copyright (C) 1997-2008
 * Christian Bauer, GPLv2 or later), which both engines share upstream:
 * - the reception thread and its semaphore are gone — the engine's 1 kHz seam
 *   polls the board (EtherPoll) and raises INTFLAG_ETHER, and the interrupt
 *   drains the board's ring;
 * - the Basilisk side keeps upstream's dispatch to protocol handlers, in a
 *   fixed table rather than a std::map;
 * - the SheepShaver side keeps upstream's glue (EtherInit, AO_*, EtherIRQ) and
 *   leaves ether_reset() to sheepshaver/platform_bits_circle.cpp, where it is
 *   the PowerPC Macintosh's restart signal;
 * - the source address of every frame sent is the interface's, since the
 *   interface's is the Mac's;
 * - the UDP tunnel is not offered: ether_start_udp_thread() refuses.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include <string.h>

#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "ether.h"
#include "ether_defs.h"

#include "board_net_circle.h"

#define FROM "okapia-ether"

#ifdef SHEEPSHAVER
static bool  net_open = false;
static uint8 ether_addr[6];
#endif

// ether_init() succeeded: the board's interface is this engine's.
static bool s_bOpen;

static uint8 s_TxFrame[1516];

#if NET_SELFTEST && !defined (SHEEPSHAVER)
static void SelfTestWatch (const uint8 *pFrame, int nLength);
static void SelfTestPoll (void);
#endif

/*
 *  Initialisation
 */

bool ether_init (void)
{
    s_bOpen = false;

    // `nonet` is not read: `ether` decides, in BoardNetStart().
    if (!BoardNetStart ())
    {
        return false;
    }
    memcpy (ether_addr, BoardNetMAC (), 6);
    BoardNetListen (true);
    s_bOpen = true;
    return true;
}

void ether_exit (void)
{
    if (s_bOpen)
    {
        BoardNetListen (false);
    }
    s_bOpen = false;
}

static int16 ether_do_write (int nLength)
{
    if (nLength < 14)
    {
        return noErr;
    }
    memcpy (s_TxFrame + 6, ether_addr, 6);
#if NET_SELFTEST && !defined (SHEEPSHAVER)
    SelfTestWatch (s_TxFrame, nLength);
#endif
    return BoardNetSend (s_TxFrame, (unsigned) nLength) ? noErr : -95;   // excessCollsns
}

#ifndef SHEEPSHAVER

/*
 *  The 68k side: Basilisk's .ENET driver
 */

// Upstream's map from packet type to the handler AppleTalk, MacTCP or Open
// Transport attached. Few protocols ever attach; sixteen is room to spare.
static const unsigned MAX_PROTOCOLS = 16;

struct TProtocol
{
    uint16 nType;
    uint32 nHandler;
};

static TProtocol s_Protocols[MAX_PROTOCOLS];
static unsigned  s_nProtocols;

void ether_reset (void)
{
    s_nProtocols = 0;
}

int16 ether_add_multicast (uint32 pb)
{
    return BoardNetJoinGroup (Mac2HostAddr (pb + eMultiAddr)) ? (int16) noErr : (int16) eMultiErr;
}

int16 ether_del_multicast (uint32 pb)
{
    return BoardNetLeaveGroup (Mac2HostAddr (pb + eMultiAddr)) ? (int16) noErr : (int16) eMultiErr;
}

int16 ether_attach_ph (uint16 type, uint32 handler)
{
    for (unsigned i = 0; i < s_nProtocols; i++)
    {
        if (s_Protocols[i].nType == type)
        {
            return lapProtErr;
        }
    }
    if (s_nProtocols >= MAX_PROTOCOLS)
    {
        return lapProtErr;
    }
    s_Protocols[s_nProtocols].nType = type;
    s_Protocols[s_nProtocols].nHandler = handler;
    s_nProtocols++;
    return noErr;
}

int16 ether_detach_ph (uint16 type)
{
    for (unsigned i = 0; i < s_nProtocols; i++)
    {
        if (s_Protocols[i].nType == type)
        {
            s_Protocols[i] = s_Protocols[--s_nProtocols];
            return noErr;
        }
    }
    return lapProtErr;
}

int16 ether_write (uint32 wds)
{
    return ether_do_write (ether_wds_to_buffer (wds, s_TxFrame));
}

bool ether_start_udp_thread (int socket_fd)
{
    return false;
}

void ether_stop_udp_thread (void)
{
}

// Upstream's ether_dispatch_packet(), unchanged but for the table.
static void ether_dispatch_packet (uint32 p, uint32 length)
{
    const uint16 type = ReadMacInt16 (p + 12);

    // An 802.3 frame carries a length where Ethernet II carries a type, and
    // goes to the handler attached for type 0 (AppleTalk Phase 2).
    const uint16 search_type = type <= 1500 ? 0 : type;
    uint32 handler = 0;
    for (unsigned i = 0; i < s_nProtocols; i++)
    {
        if (s_Protocols[i].nType == search_type)
        {
            handler = s_Protocols[i].nHandler;
            break;
        }
    }
    if (handler == 0)
    {
        return;
    }

    Mac2Mac_memcpy (ether_data + ed_RHA, p, 14);

    M68kRegisters r;
    r.d[0] = type;                              // packet type
    r.d[1] = length - 14;                       // what ReadPacket may still read
    r.a[0] = p + 14;                            // the packet, after its header
    r.a[3] = ether_data + ed_RHA + 14;          // behind the header in the RHA
    r.a[4] = ether_data + ed_ReadPacket;        // ReadPacket and ReadRest
    Execute68k (handler, &r);
}

void EtherInterrupt (void)
{
    if (!s_bOpen)
    {
        return;
    }
    EthernetPacket ether_packet;
    const uint32 packet = ether_packet.addr ();
    unsigned nLength;
    while ((nLength = BoardNetReceive (Mac2HostAddr (packet))) != 0)
    {
        ether_dispatch_packet (packet, nLength);
    }
}

// Whether the Macintosh is listening: a protocol attached is the only sign
// Basilisk's driver gives that the System is far enough to take a frame.
static bool EtherListening (void)
{
    return s_nProtocols != 0;
}

#if NET_SELFTEST

/*
 *  The receive path, proved without a network (OKAPIA_TRACE=1)
 *
 *  QEMU's USB network adapter never hands Circle a frame (Circle's
 *  doc/qemu.txt: reception needs a patched QEMU), so under QEMU nothing shows
 *  that a frame received reaches the Macintosh. This does: it learns the
 *  AppleTalk address the Mac probes for as EtherTalk starts, waits until the
 *  probing is over, puts an AARP request for that address in the board's ring
 *  as if it had arrived, and watches for the Mac's reply going out. Ring,
 *  interrupt, dispatch to the protocol handler and AppleTalk itself are all on
 *  that path. It needs a System with EtherTalk active, and says so if there
 *  was none.
 */

static const uint8 SELFTEST_MAC[6] = { 0x02, 0x00, 0x00, 0x0e, 0x7a, 0x01 };
static const uint8 AARP_SNAP[8]    = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x80, 0xf3 };
static const unsigned SELFTEST_WAIT = 4000;     // seam visits, about 4 s
static const unsigned SELFTEST_TRIES = 5;

static bool     s_bSelfAddress, s_bSelfDone;
static uint8    s_SelfAppleTalk[4];
static unsigned s_nSelfVisits, s_nSelfTries;

static void SelfTestWatch (const uint8 *pFrame, int nLength)
{
    if (s_bSelfDone || nLength < 40 || memcmp (pFrame + 14, AARP_SNAP, sizeof AARP_SNAP) != 0)
    {
        return;
    }
    const unsigned nFunction = pFrame[28] << 8 | pFrame[29];
    if (nFunction == 3)                         // a probe: the address it wants
    {
        memcpy (s_SelfAppleTalk, pFrame + 36, 4);
        s_bSelfAddress = true;
        s_nSelfVisits = 0;
    }
    else if (nFunction == 2 && memcmp (pFrame, SELFTEST_MAC, 6) == 0)
    {
        s_bSelfDone = true;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Network self-test: the Macintosh answered an AARP request "
                                "for %u.%u (try %u)",
                                s_SelfAppleTalk[1] << 8 | s_SelfAppleTalk[2],
                                s_SelfAppleTalk[3], s_nSelfTries);
    }
}

static void SelfTestPoll (void)
{
    if (!s_bSelfAddress || s_bSelfDone || ++s_nSelfVisits < SELFTEST_WAIT)
    {
        return;
    }
    s_nSelfVisits = 0;
    if (s_nSelfTries >= SELFTEST_TRIES)
    {
        s_bSelfDone = true;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "Network self-test: no answer to %u AARP requests", s_nSelfTries);
        return;
    }
    s_nSelfTries++;

    uint8 Frame[60] = { 0 };
    memcpy (Frame, ether_addr, 6);
    memcpy (Frame + 6, SELFTEST_MAC, 6);
    Frame[12] = 0;
    Frame[13] = sizeof AARP_SNAP + 28;          // an 802.3 length
    memcpy (Frame + 14, AARP_SNAP, sizeof AARP_SNAP);
    static const uint8 Request[8] = { 0x00, 0x01, 0x80, 0x9b, 6, 4, 0x00, 0x01 };
    memcpy (Frame + 22, Request, sizeof Request);
    memcpy (Frame + 30, SELFTEST_MAC, 6);
    static const uint8 Sender[4] = { 0x00, 0x00, 0xfe, 0x01 };      // 254.1
    memcpy (Frame + 36, Sender, 4);
    memcpy (Frame + 46, s_SelfAppleTalk, 4);    // target hardware address left zero
    BoardNetInject (Frame, sizeof Frame);
}

#endif  // NET_SELFTEST

#else   // SHEEPSHAVER

/*
 *  The PowerPC side: SheepShaver's DLPI driver for Open Transport
 */

void EtherInit (void)
{
    // Upstream returns here on `nonet`; see ether_init().
    net_open = ether_init ();
}

void EtherExit (void)
{
    ether_exit ();
    net_open = false;
}

void AO_get_ethernet_address (uint32 arg)
{
    uint8 *addr = Mac2HostAddr (arg);
    if (net_open)
    {
        memcpy (addr, ether_addr, 6);
    }
    else
    {
        // Upstream's placeholder, for a System that asks with no network.
        static const uint8 Placeholder[6] = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc };
        memcpy (addr, Placeholder, 6);
    }
}

void AO_enable_multicast (uint32 addr)
{
    if (net_open)
    {
        BoardNetJoinGroup (Mac2HostAddr (addr));
    }
}

void AO_disable_multicast (uint32 addr)
{
    if (net_open)
    {
        BoardNetLeaveGroup (Mac2HostAddr (addr));
    }
}

void AO_transmit_packet (uint32 mp)
{
    if (net_open)
    {
        switch (ether_do_write (ether_msgb_to_buffer (mp, s_TxFrame)))
        {
        case noErr:
            num_tx_packets++;
            break;
        default:
            num_tx_buffer_full++;
            break;
        }
    }
}

void EtherIRQ (void)
{
    num_ether_irq++;
    OTEnterInterrupt ();
    if (s_bOpen)
    {
        EthernetPacket ether_packet;
        const uint32 packet = ether_packet.addr ();
        unsigned nLength;
        while ((nLength = BoardNetReceive (Mac2HostAddr (packet))) != 0)
        {
            ether_dispatch_packet (packet, nLength);
        }
    }
    OTLeaveInterrupt ();
}

static bool EtherListening (void)
{
    return ether_driver_opened;
}

/*
 *  What Open Transport does with the driver, said in the log
 *
 *  SheepShaver's DLPI module (ether.cpp) is entered by the PowerPC Mac
 *  through native operations (sheepshaver_glue.cpp): InitStreamModule when
 *  Open Transport loads it, ether_open for every stream. Upstream says nothing
 *  outside a debug build, and "Open Transport does not load" has many causes —
 *  a missing OT library symbol fails InitStreamModule, a driver never asked for
 *  means the System did not pick the ethernet node. Wrapped (ENGINE_WRAPS), so
 *  external/ stays untouched.
 */

extern uint8 __real__Z16InitStreamModulePv (void *theID) __asm__ ("__real__Z16InitStreamModulePv");
extern void  __real__Z21TerminateStreamModulev (void) __asm__ ("__real__Z21TerminateStreamModulev");
extern int   __real__Z10ether_openP5queuePviiS1_ (queue_t *rdq, void *dev, int flag, int sflag,
                                                  void *creds) __asm__ ("__real__Z10ether_openP5queuePviiS1_");

extern "C" uint8 __wrap__Z16InitStreamModulePv (void *theID)
{
    const uint8 nResult = __real__Z16InitStreamModulePv (theID);
    CLogger::Get ()->Write (FROM, LogNotice, "Open Transport loaded the Ethernet driver: %s",
                            nResult ? "ready" : "refused (an Open Transport library symbol is missing, "
                                                "or the network did not start)");
    return nResult;
}

extern "C" void __wrap__Z21TerminateStreamModulev (void)
{
    CLogger::Get ()->Write (FROM, LogNotice, "Open Transport unloaded the Ethernet driver");
    __real__Z21TerminateStreamModulev ();
}

extern "C" int __wrap__Z10ether_openP5queuePviiS1_ (queue_t *rdq, void *dev, int flag, int sflag,
                                                     void *creds)
{
    const int nResult = __real__Z10ether_openP5queuePviiS1_ (rdq, dev, flag, sflag, creds);
    static unsigned s_nOpens;
    if (++s_nOpens <= 8)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Open Transport opened an Ethernet stream: %d (%s)",
                                nResult, net_open ? "network on" : "no network");
    }
    return nResult;
}

#endif  // SHEEPSHAVER

/*
 *  From the engine's periodic seam
 *
 *  The board's interface is polled here, on the emulation thread, about a
 *  thousand times a second; this is also what lets the Wi-Fi's tasks run. A
 *  frame waiting raises the Ethernet interrupt, which the Macintosh takes at
 *  its next chance and which empties the ring.
 */

void EtherPoll (void)
{
    BoardNetPoll ();
#if NET_SELFTEST && !defined (SHEEPSHAVER)
    if (s_bOpen && EtherListening ())
    {
        SelfTestPoll ();
    }
#endif

    if (!s_bOpen || !BoardNetPending ())
    {
        return;
    }
    if (!EtherListening ())
    {
        // Nothing may allocate a frame in the System heap before the System
        // has said it wants one.
        BoardNetDiscard ();
        return;
    }
    SetInterruptFlag (INTFLAG_ETHER);
    TriggerInterrupt ();
}
