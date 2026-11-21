//
// board_net_desktop.cpp — the board's network interface, played by slirp.
//
// ether_okapia.cpp, the Pi's Ethernet driver for the Macintosh, is compiled
// unchanged: it polls the board's interface from the engine's 1 kHz seam,
// hands the Macintosh what arrived, and sends what it wrote. On the Pi those
// frames cross the Pi's own port with the Pi's MAC address; here they cross
// upstream's slirp, a whole TCP/IP gateway in user space: the Macintosh gets
// 10.0.2.15 by DHCP behind the host's address, with no privilege asked of
// anyone (NAT). `ether eth` asks for it, as for the cable on the board.
//
// Everything runs on the emulation thread, from the seam, as on the board:
// slirp is not thread-safe and needs no thread of its own when polled.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_circle.h"
#include "board_net_okapia.h"
#include "okapia_host.h"

#include "sysdeps.h"
#include "prefs.h"

#include <string.h>
#include <sys/select.h>

extern "C" {
int  slirp_init (void);
int  slirp_select_fill (int *pnfds, fd_set *readfds, fd_set *writefds, fd_set *xfds);
void slirp_select_poll (fd_set *readfds, fd_set *writefds, fd_set *xfds);
void slirp_input (const uint8 *pkt, int pkt_len);
}

#define FROM "okapia-net"

static bool s_bTried, s_bUp, s_bListen;

// Received frames, waiting for the Macintosh: a ring of whole frames.
enum { RING = 64, FRAME = 1518 };
static u8       s_Ring[RING][FRAME];
static unsigned s_Length[RING];
static unsigned s_nHead, s_nTail;

// slirp's guest address is 10.0.2.15; the MAC is any locally administered one.
static const u8 MAC[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

bool BoardNetStart (void)
{
    if (s_bTried)
    {
        return s_bUp;
    }
    s_bTried = true;
    const char *pEther = PrefsFindString ("ether");
    if (pEther == 0 || (strcmp (pEther, "eth") != 0 && strcmp (pEther, "slirp") != 0))
    {
        HostLog (FROM, HostLogNotice, "No network (ether unset)");
        return false;
    }
    if (slirp_init () < 0)
    {
        HostLog (FROM, HostLogWarning, "Network: slirp did not start");
        return false;
    }
    s_bUp = true;
    HostLog (FROM, HostLogNotice, "Network: shared with this computer (slirp NAT), "
                                  "the Macintosh at 10.0.2.15 by DHCP");
    return true;
}

TBoardNetKind BoardNetKind (void)          { return s_bUp ? BoardNetWired : BoardNetNone; }
bool BoardNetTried (void)                  { return s_bTried; }
bool BoardNetLinkUp (unsigned *pMbps)      { *pMbps = 0; return s_bUp; }
const u8 *BoardNetMAC (void)               { return MAC; }
void BoardNetListen (bool bListen)         { s_bListen = bListen; }
bool BoardNetJoinGroup (const u8 *)        { return true; }
bool BoardNetLeaveGroup (const u8 *)       { return true; }

// slirp's side: a frame for the Macintosh.
extern "C" int slirp_can_output (void)
{
    return s_bListen;
}

extern "C" void slirp_output (const uint8 *pkt, int pkt_len)
{
    const unsigned nNext = (s_nHead + 1) % RING;
    if (!s_bListen || pkt_len <= 0 || pkt_len > FRAME || nNext == s_nTail)
    {
        return;                         // dropped, as a full ring on the board
    }
    memcpy (s_Ring[s_nHead], pkt, (size_t) pkt_len);
    s_Length[s_nHead] = (unsigned) pkt_len;
    s_nHead = nNext;
}

bool BoardNetSend (const u8 *pFrame, unsigned nLength)
{
    if (!s_bUp)
    {
        return false;
    }
    slirp_input (pFrame, (int) nLength);
    return true;
}

bool BoardNetInject (const u8 *pFrame, unsigned nLength)
{
    slirp_output (pFrame, (int) nLength);
    return true;
}

void BoardNetPoll (void)
{
    if (!s_bUp)
    {
        return;
    }
    fd_set Read, Write, Except;
    FD_ZERO (&Read);
    FD_ZERO (&Write);
    FD_ZERO (&Except);
    int nFds = -1;
    slirp_select_fill (&nFds, &Read, &Write, &Except);
    struct timeval Zero = { 0, 0 };
    if (select (nFds + 1, &Read, &Write, &Except, &Zero) >= 0)
    {
        slirp_select_poll (&Read, &Write, &Except);
    }
}

bool BoardNetPending (void)
{
    return s_nHead != s_nTail;
}

void BoardNetDiscard (void)
{
    s_nTail = s_nHead;
}

unsigned BoardNetReceive (u8 *pBuffer)
{
    if (s_nHead == s_nTail)
    {
        return 0;
    }
    const unsigned nLength = s_Length[s_nTail];
    memcpy (pBuffer, s_Ring[s_nTail], nLength);
    s_nTail = (s_nTail + 1) % RING;
    return nLength;
}
