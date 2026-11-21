/*
 * net_clock.cpp — the time from the network, asked in raw frames. See
 * net_clock.h.
 *
 * References: RFC 2131 and 2132 (DHCP and its options), RFC 826 (ARP),
 * RFC 1035 (DNS), RFC 4330 (SNTP v4), RFC 768 and 791 (UDP, IPv4).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "net_clock.h"

#include <string.h>

/*
 *  Where things are in a frame
 */
static const unsigned ETH_HEADER  = 14;
static const unsigned IP_HEADER   = 20;
static const unsigned UDP_HEADER  = 8;
static const unsigned UDP_PAYLOAD = ETH_HEADER + IP_HEADER + UDP_HEADER;    // 42

static const uint16_t TYPE_IPV4 = 0x0800;
static const uint16_t TYPE_ARP  = 0x0806;
static const uint8_t  PROTO_UDP = 17;

static const uint16_t PORT_DHCP_SERVER = 67;
static const uint16_t PORT_DHCP_CLIENT = 68;
static const uint16_t PORT_DNS         = 53;
static const uint16_t PORT_NTP         = 123;

// DHCP message types (RFC 2132, 9.6).
static const uint8_t DHCP_DISCOVER = 1;
static const uint8_t DHCP_OFFER    = 2;
static const uint8_t DHCP_REQUEST  = 3;
static const uint8_t DHCP_ACK      = 5;
static const uint8_t DHCP_NAK      = 6;

// The fixed part of a BOOTP message, and the 300 bytes some servers still
// insist on (RFC 1542, 3.1.1, by way of every embedded client).
static const unsigned DHCP_OPTIONS = 240;
static const unsigned DHCP_MINIMUM = 300;

static const unsigned NTP_PACKET = 48;

// 1900 to 1970: seventy years with seventeen leap days.
static const uint32_t NTP_UNIX_DIFF = 2208988800U;

/*
 *  How long each step waits, and how often it asks again
 */
static const unsigned LEASE_WAIT    = 2000;
static const unsigned LEASE_TRIES   = 4;
static const unsigned REQUEST_WAIT  = 2000;
static const unsigned REQUEST_TRIES = 3;
static const unsigned ARP_WAIT      = 1000;
static const unsigned ARP_TRIES     = 3;
static const unsigned DNS_WAIT      = 2000;
static const unsigned DNS_TRIES     = 4;
static const unsigned NTP_WAIT      = 2000;
static const unsigned NTP_TRIES     = 2;        // per address

static const uint8_t BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/*
 *  Bytes
 */
static void Put16 (uint8_t *p, uint16_t n)
{
    p[0] = (uint8_t) (n >> 8);
    p[1] = (uint8_t) n;
}

static void Put32 (uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t) (n >> 24);
    p[1] = (uint8_t) (n >> 16);
    p[2] = (uint8_t) (n >> 8);
    p[3] = (uint8_t) n;
}

static uint16_t Get16 (const uint8_t *p)
{
    return (uint16_t) (p[0] << 8 | p[1]);
}

static uint32_t Get32 (const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}

static uint32_t Random (TNetClock *p)
{
    // xorshift32: nothing here guards anything worth more than a clock, and a
    // transaction number only has to differ from the Macintosh's own.
    uint32_t x = p->nRandom;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    p->nRandom = x != 0 ? x : 0x2545F491u;
    return p->nRandom;
}

static bool Due (unsigned nNow, unsigned nDeadline)
{
    return (int) (nNow - nDeadline) >= 0;
}

static uint32_t Sum (uint32_t nSum, const uint8_t *p, unsigned nLength)
{
    for (unsigned i = 0; i + 1 < nLength; i += 2)
    {
        nSum += Get16 (p + i);
    }
    if (nLength & 1)
    {
        nSum += (uint32_t) p[nLength - 1] << 8;
    }
    return nSum;
}

static uint16_t Fold (uint32_t nSum)
{
    while (nSum >> 16)
    {
        nSum = (nSum & 0xFFFF) + (nSum >> 16);
    }
    return (uint16_t) ~nSum;
}

/*
 *  Frames out
 *
 *  The payload is written in place at UDP_PAYLOAD, then the headers around it.
 */
unsigned NetUdpBuild (uint8_t *pOut, const uint8_t *pFrom, const uint8_t *pTo,
                      uint32_t nSource, uint32_t nDest, uint16_t nSourcePort, uint16_t nDestPort,
                      unsigned nPayload, uint16_t nIpId, bool bChecksum)
{
    memcpy (pOut, pTo, 6);
    memcpy (pOut + 6, pFrom, 6);
    Put16 (pOut + 12, TYPE_IPV4);

    uint8_t *pIP = pOut + ETH_HEADER;
    const unsigned nTotal = IP_HEADER + UDP_HEADER + nPayload;
    pIP[0] = 0x45;
    pIP[1] = 0;
    Put16 (pIP + 2, (uint16_t) nTotal);
    Put16 (pIP + 4, nIpId);
    Put16 (pIP + 6, 0);
    pIP[8] = 64;
    pIP[9] = PROTO_UDP;
    Put16 (pIP + 10, 0);
    Put32 (pIP + 12, nSource);
    Put32 (pIP + 16, nDest);
    Put16 (pIP + 10, Fold (Sum (0, pIP, IP_HEADER)));

    uint8_t *pUDP = pIP + IP_HEADER;
    Put16 (pUDP, nSourcePort);
    Put16 (pUDP + 2, nDestPort);
    Put16 (pUDP + 4, (uint16_t) (UDP_HEADER + nPayload));
    Put16 (pUDP + 6, 0);
    // Zero is "no checksum" in IPv4 (RFC 768): what a frame whose payload is
    // rewritten for every send carries, rather than a sum computed each time.
    if (bChecksum)
    {
        uint32_t nSum = Sum (0, pIP + 12, 8);       // the pseudo-header's addresses
        nSum += PROTO_UDP;
        nSum += UDP_HEADER + nPayload;
        nSum = Sum (nSum, pUDP, UDP_HEADER + nPayload);
        uint16_t nCheck = Fold (nSum);
        Put16 (pUDP + 6, nCheck != 0 ? nCheck : 0xFFFF);
    }

    return ETH_HEADER + nTotal;
}

static unsigned UdpFinish (TNetClock *p, uint8_t *pOut, const uint8_t *pTo,
                           uint32_t nSource, uint32_t nDest,
                           uint16_t nSourcePort, uint16_t nDestPort, unsigned nPayload)
{
    return NetUdpBuild (pOut, p->MAC, pTo, nSource, nDest, nSourcePort, nDestPort, nPayload,
                        p->nIpId++, true);
}

static unsigned DhcpFrame (TNetClock *p, uint8_t *pOut, uint8_t nType)
{
    uint8_t *d = pOut + UDP_PAYLOAD;
    memset (d, 0, DHCP_MINIMUM);
    d[0] = 1;                           // BOOTREQUEST
    d[1] = 1;                           // Ethernet
    d[2] = 6;
    Put32 (d + 4, p->nXid);
    // Broadcast the answer: there is no address to send it to yet, and the
    // Macintosh holds none we could borrow.
    Put16 (d + 10, 0x8000);
    memcpy (d + 28, p->MAC, 6);
    d[236] = 99; d[237] = 130; d[238] = 83; d[239] = 99;

    unsigned n = DHCP_OPTIONS;
    d[n++] = 53; d[n++] = 1; d[n++] = nType;
    if (nType == DHCP_REQUEST)
    {
        d[n++] = 50; d[n++] = 4; Put32 (d + n, p->nOffered);    n += 4;
        d[n++] = 54; d[n++] = 4; Put32 (d + n, p->nDhcpServer); n += 4;
    }
    // Subnet mask, router, name servers: the three things the rest needs.
    d[n++] = 55; d[n++] = 3; d[n++] = 1; d[n++] = 3; d[n++] = 6;
    d[n++] = 255;

    return UdpFinish (p, pOut, BROADCAST, 0, 0xFFFFFFFFu,
                      PORT_DHCP_CLIENT, PORT_DHCP_SERVER, DHCP_MINIMUM);
}

unsigned NetArpBuild (uint8_t *pOut, const uint8_t *pFrom, uint32_t nFrom, uint16_t nOp,
                      const uint8_t *pTo, const uint8_t *pTargetMAC, uint32_t nTarget)
{
    memcpy (pOut, pTo, 6);
    memcpy (pOut + 6, pFrom, 6);
    Put16 (pOut + 12, TYPE_ARP);
    uint8_t *a = pOut + ETH_HEADER;
    Put16 (a, 1);
    Put16 (a + 2, TYPE_IPV4);
    a[4] = 6;
    a[5] = 4;
    Put16 (a + 6, nOp);
    memcpy (a + 8, pFrom, 6);
    Put32 (a + 14, nFrom);
    memcpy (a + 18, pTargetMAC, 6);
    Put32 (a + 24, nTarget);
    return ETH_HEADER + 28;
}

static unsigned ArpFrame (TNetClock *p, uint8_t *pOut, uint16_t nOp, const uint8_t *pTo,
                          const uint8_t *pTargetMAC, uint32_t nTarget)
{
    return NetArpBuild (pOut, p->MAC, p->nAddress, nOp, pTo, pTargetMAC, nTarget);
}

static unsigned DnsFrame (TNetClock *p, uint8_t *pOut, uint32_t nTo)
{
    uint8_t *d = pOut + UDP_PAYLOAD;
    Put16 (d, p->nDnsId);
    Put16 (d + 2, 0x0100);              // a standard query, recursion desired
    Put16 (d + 4, 1);
    Put16 (d + 6, 0);
    Put16 (d + 8, 0);
    Put16 (d + 10, 0);
    unsigned n = 12;
    const char *pName = p->Server;
    while (*pName != '\0')
    {
        const char *pDot = strchr (pName, '.');
        const unsigned nLabel = pDot != 0 ? (unsigned) (pDot - pName) : (unsigned) strlen (pName);
        d[n++] = (uint8_t) nLabel;
        memcpy (d + n, pName, nLabel);
        n += nLabel;
        pName += nLabel;
        if (*pName == '.')
        {
            pName++;
        }
    }
    d[n++] = 0;
    Put16 (d + n, 1);                   // A
    Put16 (d + n + 2, 1);               // IN
    n += 4;
    return UdpFinish (p, pOut, p->HopMAC, p->nAddress, nTo, p->nPort, PORT_DNS, n);
}

static unsigned NtpFrame (TNetClock *p, uint8_t *pOut, uint32_t nTo)
{
    uint8_t *d = pOut + UDP_PAYLOAD;
    memset (d, 0, NTP_PACKET);
    d[0] = 0x23;                        // no leap warning, version 4, client
    // The transmit timestamp is not the time — this machine does not know it,
    // which is the point — but a nonce the server echoes back as the
    // originate timestamp, so an answer to someone else's question is not
    // taken for ours (RFC 4330, section 5).
    memcpy (d + 40, p->Nonce, 8);
    return UdpFinish (p, pOut, p->HopMAC, p->nAddress, nTo, p->nPort, PORT_NTP, NTP_PACKET);
}

/*
 *  Names and addresses
 */
static bool ParseAddress (const char *pText, uint32_t *pAddress)
{
    uint32_t nAddress = 0;
    for (unsigned i = 0; i < 4; i++)
    {
        if (*pText < '0' || *pText > '9')
        {
            return false;
        }
        unsigned nPart = 0;
        unsigned nDigits = 0;
        while (*pText >= '0' && *pText <= '9')
        {
            nPart = nPart * 10 + (unsigned) (*pText++ - '0');
            if (++nDigits > 3 || nPart > 255)
            {
                return false;
            }
        }
        nAddress = nAddress << 8 | nPart;
        if (i < 3 && *pText++ != '.')
        {
            return false;
        }
    }
    if (*pText != '\0')
    {
        return false;
    }
    *pAddress = nAddress;
    return true;
}

// A name DNS can carry: labels of 1 to 63 characters, 253 in all.
static bool NameIsValid (const char *pName)
{
    const size_t nLength = strlen (pName);
    if (nLength == 0 || nLength > 253)
    {
        return false;
    }
    unsigned nLabel = 0;
    for (const char *p = pName; *p != '\0'; p++)
    {
        if (*p == '.')
        {
            if (nLabel == 0)
            {
                return false;
            }
            nLabel = 0;
            continue;
        }
        const char c = *p;
        const bool bOK = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                      || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!bOK || ++nLabel > 63)
        {
            return false;
        }
    }
    return true;
}

const char *NetClockAddress (uint32_t nAddress, char *pBuffer)
{
    char *p = pBuffer;
    for (int i = 3; i >= 0; i--)
    {
        unsigned n = (nAddress >> (i * 8)) & 0xFF;
        if (n >= 100) *p++ = (char) ('0' + n / 100);
        if (n >= 10)  *p++ = (char) ('0' + (n / 10) % 10);
        *p++ = (char) ('0' + n % 10);
        if (i != 0)
        {
            *p++ = '.';
        }
    }
    *p = '\0';
    return pBuffer;
}

uint32_t NetClockUnixOf (uint32_t nNtpSeconds)
{
    // Era 0 ends on 7 February 2036. An answer with the top bit clear cannot
    // be from before 1968, so it is from era 1, and the subtraction wraps into
    // the right place on its own: 2^32 - 2208988800 + n.
    return nNtpSeconds - NTP_UNIX_DIFF;
}

/*
 *  The steps
 */
static void Enter (TNetClock *p, TNetClockState State, unsigned nNow)
{
    p->State = State;
    p->nTries = 0;
    p->nDeadline = nNow;                // the next Step sends at once
}

static void Fail (TNetClock *p, TNetClockFailure Failure)
{
    p->State = NetClockFailed;
    p->Failure = Failure;
}

static bool OnLink (const TNetClock *p, uint32_t nAddress)
{
    return ((nAddress ^ p->nAddress) & p->nMask) == 0;
}

// The machine a packet for nTarget is handed to.
static void GoThrough (TNetClock *p, uint32_t nTarget, TNetClockState Then, unsigned nNow)
{
    const uint32_t nHop = OnLink (p, nTarget) ? nTarget : p->nRouter;
    if (nHop == 0)
    {
        Fail (p, NetClockNoRouter);
        return;
    }
    if (p->bHopKnown && nHop == p->nHop)
    {
        Enter (p, Then, nNow);
        return;
    }
    p->nHop = nHop;
    p->bHopKnown = false;
    Enter (p, NetClockResolveRouter, nNow);
}

static void AfterHop (TNetClock *p, unsigned nNow)
{
    if (p->nServers == 0)
    {
        Enter (p, NetClockLookUp, nNow);
    }
    else
    {
        Enter (p, NetClockAsk, nNow);
    }
}

static void AskServer (TNetClock *p, unsigned nIndex, unsigned nNow)
{
    if (nIndex >= p->nServers)
    {
        Fail (p, NetClockNoAnswer);
        return;
    }
    p->nServer = nIndex;
    GoThrough (p, p->Servers[nIndex], NetClockAsk, nNow);
}

void NetClockBegin (TNetClock *p, const uint8_t *pMAC, const char *pServer,
                    uint32_t nRandom, unsigned nNow)
{
    memset (p, 0, sizeof *p);
    memcpy (p->MAC, pMAC, 6);
    strncpy (p->Server, pServer, sizeof p->Server - 1);
    p->nRandom = nRandom ^ 0x9E3779B9u;
    if (p->nRandom == 0)
    {
        p->nRandom = 1;
    }
    p->nXid   = Random (p);
    p->nPort  = (uint16_t) (49152 + Random (p) % 16384);
    p->nDnsId = (uint16_t) Random (p);
    p->nIpId  = (uint16_t) Random (p);
    for (unsigned i = 0; i < 8; i += 4)
    {
        Put32 (p->Nonce + i, Random (p));
    }

    uint32_t nAddress;
    if (ParseAddress (p->Server, &nAddress))
    {
        p->Servers[0] = nAddress;
        p->nServers = 1;
    }
    else if (!NameIsValid (p->Server))
    {
        Fail (p, NetClockNoName);
        return;
    }
    Enter (p, NetClockLease, nNow);
}

bool NetClockBusy (const TNetClock *p)
{
    return p->State != NetClockIdle && p->State != NetClockDone && p->State != NetClockFailed;
}

void NetClockStep (TNetClock *p, unsigned nNow, uint8_t *pOut, unsigned *pnOut)
{
    *pnOut = 0;
    if (!NetClockBusy (p) || !Due (nNow, p->nDeadline))
    {
        return;
    }

    switch (p->State)
    {
    case NetClockLease:
        if (p->nTries >= LEASE_TRIES)
        {
            Fail (p, NetClockNoLease);
            return;
        }
        *pnOut = DhcpFrame (p, pOut, DHCP_DISCOVER);
        p->nDeadline = nNow + LEASE_WAIT;
        break;

    case NetClockRequest:
        if (p->nTries >= REQUEST_TRIES)
        {
            Fail (p, NetClockNoLease);
            return;
        }
        *pnOut = DhcpFrame (p, pOut, DHCP_REQUEST);
        p->nDeadline = nNow + REQUEST_WAIT;
        break;

    case NetClockResolveRouter:
        if (p->nTries >= ARP_TRIES)
        {
            Fail (p, NetClockNoRouter);
            return;
        }
        {
            static const uint8_t Zero[6] = { 0, 0, 0, 0, 0, 0 };
            *pnOut = ArpFrame (p, pOut, 1, BROADCAST, Zero, p->nHop);
        }
        p->nDeadline = nNow + ARP_WAIT;
        break;

    case NetClockLookUp:
        if (p->nTries >= DNS_TRIES || p->nDnsCount == 0)
        {
            Fail (p, NetClockNoName);
            return;
        }
        // The second name server, when there is one, on alternate tries. Both
        // are reached through the same hop: a name server is almost always
        // the router itself, and when it is not the router forwards.
        *pnOut = DnsFrame (p, pOut, p->nDns[p->nTries % p->nDnsCount]);
        p->nDeadline = nNow + DNS_WAIT;
        break;

    case NetClockAsk:
        if (p->nTries >= NTP_TRIES)
        {
            AskServer (p, p->nServer + 1, nNow);
            if (p->State != NetClockFailed)
            {
                NetClockStep (p, nNow, pOut, pnOut);
            }
            return;
        }
        *pnOut = NtpFrame (p, pOut, p->Servers[p->nServer]);
        p->nAsked = nNow;
        p->nDeadline = nNow + NTP_WAIT;
        break;

    default:
        return;
    }
    p->nTries++;
}

/*
 *  Frames in
 */

const uint8_t *NetUdpFind (const uint8_t *pFrame, unsigned nLength,
                             uint16_t nSourcePort, uint16_t nDestPort,
                             uint32_t *pSource, unsigned *pnPayload)
{
    if (nLength < UDP_PAYLOAD || Get16 (pFrame + 12) != TYPE_IPV4)
    {
        return 0;
    }
    const uint8_t *pIP = pFrame + ETH_HEADER;
    const unsigned nHeader = (pIP[0] & 0x0F) * 4u;
    if ((pIP[0] >> 4) != 4 || nHeader < IP_HEADER || pIP[9] != PROTO_UDP)
    {
        return 0;
    }
    // A fragment is not worth reassembling for a question this small.
    if ((Get16 (pIP + 6) & 0x3FFF) != 0)
    {
        return 0;
    }
    const unsigned nTotal = Get16 (pIP + 2);
    if (nTotal < nHeader + UDP_HEADER || ETH_HEADER + nTotal > nLength)
    {
        return 0;
    }
    const uint8_t *pUDP = pIP + nHeader;
    if (Get16 (pUDP) != nSourcePort || Get16 (pUDP + 2) != nDestPort)
    {
        return 0;
    }
    const unsigned nUDP = Get16 (pUDP + 4);
    if (nUDP < UDP_HEADER || nHeader + nUDP > nTotal)
    {
        return 0;
    }
    *pSource = Get32 (pIP + 12);
    *pnPayload = nUDP - UDP_HEADER;
    return pUDP + UDP_HEADER;
}

static bool TakeDhcp (TNetClock *p, const uint8_t *d, unsigned n, unsigned nNow,
                      uint8_t *pOut, unsigned *pnOut)
{
    if (n < DHCP_OPTIONS || d[0] != 2 || Get32 (d + 4) != p->nXid
        || memcmp (d + 28, p->MAC, 6) != 0
        || d[236] != 99 || d[237] != 130 || d[238] != 83 || d[239] != 99)
    {
        return false;
    }
    if (p->State != NetClockLease && p->State != NetClockRequest)
    {
        return true;                    // ours, but late: a repeat
    }

    uint8_t nType = 0;
    uint32_t nServer = 0, nMask = 0, nRouter = 0;
    uint32_t Dns[2] = { 0, 0 };
    unsigned nDns = 0;
    unsigned i = DHCP_OPTIONS;
    while (i < n && d[i] != 255)
    {
        if (d[i] == 0)
        {
            i++;
            continue;
        }
        if (i + 2 > n || i + 2 + d[i + 1] > n)
        {
            break;
        }
        const uint8_t nCode = d[i];
        const unsigned nSize = d[i + 1];
        const uint8_t *v = d + i + 2;
        if (nCode == 53 && nSize >= 1)
        {
            nType = v[0];
        }
        else if (nCode == 54 && nSize >= 4)
        {
            nServer = Get32 (v);
        }
        else if (nCode == 1 && nSize >= 4)
        {
            nMask = Get32 (v);
        }
        else if (nCode == 3 && nSize >= 4)
        {
            nRouter = Get32 (v);
        }
        else if (nCode == 6)
        {
            for (unsigned k = 0; k + 4 <= nSize && nDns < 2; k += 4)
            {
                Dns[nDns++] = Get32 (v + k);
            }
        }
        i += 2 + nSize;
    }

    if (p->State == NetClockLease && nType == DHCP_OFFER && Get32 (d + 16) != 0)
    {
        p->nOffered = Get32 (d + 16);
        p->nDhcpServer = nServer;
        Enter (p, NetClockRequest, nNow);
        NetClockStep (p, nNow, pOut, pnOut);
    }
    else if (p->State == NetClockRequest && nType == DHCP_ACK)
    {
        p->nAddress = Get32 (d + 16) != 0 ? Get32 (d + 16) : p->nOffered;
        // A lease without a mask is a class C network in every home there is.
        p->nMask = nMask != 0 ? nMask : 0xFFFFFF00u;
        p->nRouter = nRouter;
        p->nDns[0] = Dns[0];
        p->nDns[1] = Dns[1];
        p->nDnsCount = nDns;
        // Nothing is released afterwards: the Macintosh shares this MAC, so
        // its own lease is this one, and giving it back would take its address
        // away from under it.
        if (p->nServers == 0 && nDns == 0)
        {
            Fail (p, NetClockNoName);   // a name, and nobody to ask it of
            return true;
        }
        // The hop is resolved first in every case, then AfterHop() decides
        // between the name and the question.
        GoThrough (p, p->nServers != 0 ? p->Servers[0] : Dns[0], NetClockAsk, nNow);
        NetClockStep (p, nNow, pOut, pnOut);
    }
    else if (p->State == NetClockRequest && nType == DHCP_NAK)
    {
        p->nXid = Random (p);
        Enter (p, NetClockLease, nNow);
        NetClockStep (p, nNow, pOut, pnOut);
    }
    return true;
}

static unsigned SkipName (const uint8_t *d, unsigned n, unsigned i)
{
    while (i < n)
    {
        const uint8_t nLabel = d[i];
        if ((nLabel & 0xC0) == 0xC0)
        {
            return i + 2 <= n ? i + 2 : 0;
        }
        if (nLabel == 0)
        {
            return i + 1;
        }
        i += 1 + nLabel;
    }
    return 0;
}

static bool TakeDns (TNetClock *p, const uint8_t *d, unsigned n, unsigned nNow,
                     uint8_t *pOut, unsigned *pnOut)
{
    if (n < 12 || Get16 (d) != p->nDnsId || (d[2] & 0x80) == 0)
    {
        return false;
    }
    if (p->State != NetClockLookUp)
    {
        return true;
    }
    const unsigned nQuestions = Get16 (d + 4);
    const unsigned nAnswers   = Get16 (d + 6);
    if ((d[3] & 0x0F) != 0)
    {
        Fail (p, NetClockNoName);       // the name does not exist, or the server refused
        return true;
    }
    unsigned i = 12;
    for (unsigned q = 0; q < nQuestions && i != 0; q++)
    {
        i = SkipName (d, n, i);
        i = i != 0 && i + 4 <= n ? i + 4 : 0;
    }
    for (unsigned a = 0; a < nAnswers && i != 0 && p->nServers < NET_CLOCK_ADDRESSES; a++)
    {
        i = SkipName (d, n, i);
        if (i == 0 || i + 10 > n)
        {
            break;
        }
        const uint16_t nType  = Get16 (d + i);
        const uint16_t nClass = Get16 (d + i + 2);
        const unsigned nData  = Get16 (d + i + 8);
        i += 10;
        if (i + nData > n)
        {
            break;
        }
        // A CNAME on the way is skipped: its target's A records follow it in
        // the same answer, which is how every resolver answers.
        if (nType == 1 && nClass == 1 && nData == 4)
        {
            p->Servers[p->nServers++] = Get32 (d + i);
        }
        i += nData;
    }
    if (p->nServers == 0)
    {
        Fail (p, NetClockNoName);
        return true;
    }
    AskServer (p, 0, nNow);
    NetClockStep (p, nNow, pOut, pnOut);
    return true;
}

static bool TakeNtp (TNetClock *p, const uint8_t *d, unsigned n, uint32_t nSource,
                     unsigned nNow, uint8_t *pOut, unsigned *pnOut)
{
    if (p->State != NetClockAsk || nSource != p->Servers[p->nServer] || n < NTP_PACKET)
    {
        return p->State == NetClockAsk || p->State == NetClockDone;
    }
    const unsigned nLeap    = d[0] >> 6;
    const unsigned nVersion = (d[0] >> 3) & 7;
    const unsigned nMode    = d[0] & 7;
    const unsigned nStratum = d[1];
    const uint32_t nSeconds = Get32 (d + 40);
    if (memcmp (d + 24, p->Nonce, 8) != 0)
    {
        return true;                    // not an answer to this question
    }
    // A server that says it is not synchronised (leap 3), or a kiss of death
    // (stratum 0), is asked no more; the next address is.
    if (nMode != 4 || nVersion < 3 || nVersion > 4 || nLeap == 3
        || nStratum == 0 || nStratum > 15 || nSeconds == 0)
    {
        AskServer (p, p->nServer + 1, nNow);
        if (p->State != NetClockFailed)
        {
            NetClockStep (p, nNow, pOut, pnOut);
        }
        return true;
    }

    // The server's transmit time, plus half the round trip: the time it took
    // the answer to arrive, on the assumption that the way there and the way
    // back are alike (RFC 4330, section 5).
    const uint32_t nFraction = Get32 (d + 44);
    unsigned nMillis = (unsigned) (((uint64_t) nFraction * 1000) >> 32);
    nMillis += (nNow - p->nAsked) / 2;
    p->nUnix    = NetClockUnixOf (nSeconds) + nMillis / 1000;
    p->nMillis  = nMillis % 1000;
    p->nAt      = nNow;
    p->nFrom    = nSource;
    p->nStratum = nStratum;
    p->State    = NetClockDone;
    return true;
}

static void TakeArp (TNetClock *p, const uint8_t *pFrame, unsigned nLength,
                     uint8_t *pOut, unsigned *pnOut)
{
    if (nLength < ETH_HEADER + 28)
    {
        return;
    }
    const uint8_t *a = pFrame + ETH_HEADER;
    if (Get16 (a) != 1 || Get16 (a + 2) != TYPE_IPV4 || a[4] != 6 || a[5] != 4)
    {
        return;
    }
    const uint16_t nOp = Get16 (a + 6);
    const uint32_t nSender = Get32 (a + 14);
    const uint32_t nTarget = Get32 (a + 24);

    if (nOp == 2 && p->State == NetClockResolveRouter && nSender == p->nHop)
    {
        memcpy (p->HopMAC, a + 8, 6);
        p->bHopKnown = true;
        return;
    }
    // Who has our address: the router asking before it delivers the answer.
    // Said even when the Macintosh says it too — it holds the same address
    // with the same MAC, so both say the same thing.
    if (nOp == 1 && p->nAddress != 0 && nTarget == p->nAddress && NetClockBusy (p)
        && p->State != NetClockLease && p->State != NetClockRequest)
    {
        *pnOut = ArpFrame (p, pOut, 2, a + 8, a + 8, nSender);
    }
}

bool NetClockTake (TNetClock *p, const uint8_t *pFrame, unsigned nLength, unsigned nNow,
                   uint8_t *pOut, unsigned *pnOut)
{
    *pnOut = 0;
    if (p->State == NetClockIdle || p->State == NetClockFailed || nLength < ETH_HEADER)
    {
        return false;
    }

    if (Get16 (pFrame + 12) == TYPE_ARP)
    {
        const bool bWasResolving = p->State == NetClockResolveRouter && !p->bHopKnown;
        TakeArp (p, pFrame, nLength, pOut, pnOut);
        if (bWasResolving && p->bHopKnown)
        {
            AfterHop (p, nNow);
            NetClockStep (p, nNow, pOut, pnOut);
        }
        return false;                   // the Macintosh may be waiting for it too
    }

    uint32_t nSource;
    unsigned nPayload;
    const uint8_t *d;
    if ((d = NetUdpFind (pFrame, nLength, PORT_DHCP_SERVER, PORT_DHCP_CLIENT, &nSource, &nPayload)) != 0)
    {
        return TakeDhcp (p, d, nPayload, nNow, pOut, pnOut);
    }
    if (p->nPort == 0)
    {
        return false;
    }
    if ((d = NetUdpFind (pFrame, nLength, PORT_DNS, p->nPort, &nSource, &nPayload)) != 0)
    {
        return TakeDns (p, d, nPayload, nNow, pOut, pnOut);
    }
    if ((d = NetUdpFind (pFrame, nLength, PORT_NTP, p->nPort, &nSource, &nPayload)) != 0)
    {
        return TakeNtp (p, d, nPayload, nSource, nNow, pOut, pnOut);
    }
    return false;
}

bool NetArpAnswer (const uint8_t *pFrame, unsigned nLength, uint32_t nWho, uint8_t *pMAC)
{
    if (nLength < ETH_HEADER + 28 || Get16 (pFrame + 12) != TYPE_ARP)
    {
        return false;
    }
    const uint8_t *a = pFrame + ETH_HEADER;
    if (Get16 (a) != 1 || Get16 (a + 2) != TYPE_IPV4 || a[4] != 6 || a[5] != 4
        || Get16 (a + 6) != 2 || Get32 (a + 14) != nWho)
    {
        return false;
    }
    memcpy (pMAC, a + 8, 6);
    return true;
}

bool NetParseAddress (const char *pText, uint32_t *pAddress)
{
    return ParseAddress (pText, pAddress);
}
