/*
 * net_clock.h — the time from the network, asked in raw frames.
 *
 * Okapia runs no network stack: the Macintosh shares the Pi's MAC address and
 * every frame the interface receives is its (docs/topics/network.md). Circle's
 * own NTP client needs Circle's whole TCP/IP stack, which would take the
 * Macintosh's frames for itself. So the one question Okapia has for the
 * network is asked here, by hand, in the fewest frames that can ask it: a DHCP
 * lease for an address, ARP for the router, DNS for the server's name, and one
 * SNTP exchange (RFC 4330). Every frame this sends or takes is told apart from
 * the Macintosh's by a transaction number or a port of its own; everything
 * else is left to the Macintosh.
 *
 * Pure: no Circle, no emulator, no clock of its own. The caller hands in the
 * time in milliseconds and the frames received, and sends what this answers.
 * That is what lets tests/host run a whole exchange against a fake router.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_NET_CLOCK_H
#define OKAPIA_NET_CLOCK_H

#include <stddef.h>
#include <stdint.h>

// The largest frame this ever sends: a DHCP request, padded to BOOTP's 300.
static const unsigned NET_CLOCK_FRAME = 400;

static const unsigned NET_CLOCK_SERVER = 64;
static const unsigned NET_CLOCK_ADDRESSES = 4;

enum TNetClockState
{
    NetClockIdle,
    NetClockLease,          // DHCPDISCOVER sent, waiting for an offer
    NetClockRequest,        // DHCPREQUEST sent, waiting for the ack
    NetClockResolveRouter,  // ARP for the next hop
    NetClockLookUp,         // DNS query for the server's name
    NetClockAsk,            // SNTP request sent
    NetClockDone,
    NetClockFailed
};

// Why it failed, for the log.
enum TNetClockFailure
{
    NetClockNoFailure,
    NetClockNoLease,        // no DHCP server answered
    NetClockNoRouter,       // the lease named no router, or it did not answer ARP
    NetClockNoName,         // the server's name did not resolve
    NetClockNoAnswer        // no NTP server answered, or none was believable
};

struct TNetClock
{
    TNetClockState   State;
    TNetClockFailure Failure;

    uint8_t  MAC[6];
    char     Server[NET_CLOCK_SERVER];
    uint32_t nRandom;               // xorshift state

    unsigned nDeadline;             // ms
    unsigned nTries;                // in the current step
    unsigned nRounds;               // DHCP rounds

    uint32_t nXid;                  // DHCP transaction
    uint16_t nPort;                 // our UDP port for DNS and NTP
    uint16_t nDnsId;
    uint16_t nIpId;

    // The lease, all in host order.
    uint32_t nOffered;
    uint32_t nDhcpServer;
    uint32_t nAddress;
    uint32_t nMask;
    uint32_t nRouter;
    uint32_t nDns[2];
    unsigned nDnsCount;

    uint32_t nHop;                  // whose MAC ARP is asking for
    uint8_t  HopMAC[6];
    bool     bHopKnown;

    uint32_t Servers[NET_CLOCK_ADDRESSES];
    unsigned nServers;
    unsigned nServer;               // the one being asked

    uint8_t  Nonce[8];              // our transmit timestamp, echoed back
    unsigned nAsked;                // ms, when the request went out

    // The answer: UTC seconds since 1970 at nAt (ms), and who gave it.
    uint32_t nUnix;
    unsigned nMillis;               // the fraction, in ms
    unsigned nAt;
    uint32_t nFrom;
    unsigned nStratum;
};

// Starts a query. pServer is a host name or a dotted address; nRandom seeds
// the transaction number, the port and the nonce.
void NetClockBegin (TNetClock *p, const uint8_t *pMAC, const char *pServer,
                    uint32_t nRandom, unsigned nNow);

// Offers a received Ethernet frame. Answers true when the frame was this
// query's and must not reach the Macintosh. May put a frame to send in pOut
// (NET_CLOCK_FRAME bytes) and its length in *pnOut — an ARP reply for our
// address is sent while the frame still goes on to the Macintosh.
bool NetClockTake (TNetClock *p, const uint8_t *pFrame, unsigned nLength, unsigned nNow,
                   uint8_t *pOut, unsigned *pnOut);

// The timers: a retry, the next step, a failure. May put a frame in pOut.
void NetClockStep (TNetClock *p, unsigned nNow, uint8_t *pOut, unsigned *pnOut);

// Whether the query is still asking.
bool NetClockBusy (const TNetClock *p);

// NTP seconds (since 1900) to Unix seconds, across the 2036 wrap: a value with
// its top bit clear is taken to be in era 1, so this holds until 2104.
uint32_t NetClockUnixOf (uint32_t nNtpSeconds);

// For the log: a dotted address in a buffer of at least 16 bytes.
const char *NetClockAddress (uint32_t nAddress, char *pBuffer);

/*
 *  The frames, for whoever else asks the network something on the shared MAC
 *  (net_bench_circle.cpp). Host order everywhere; the payload of a UDP frame
 *  is at NET_UDP_PAYLOAD, written there before NetUdpBuild() wraps it.
 */
static const unsigned NET_UDP_PAYLOAD = 42;

unsigned NetUdpBuild (uint8_t *pOut, const uint8_t *pFrom, const uint8_t *pTo,
                      uint32_t nSource, uint32_t nDest, uint16_t nSourcePort, uint16_t nDestPort,
                      unsigned nPayload, uint16_t nIpId, bool bChecksum);

// The UDP payload of an IPv4 frame from nSourcePort to nDestPort, or 0.
const uint8_t *NetUdpFind (const uint8_t *pFrame, unsigned nLength,
                           uint16_t nSourcePort, uint16_t nDestPort,
                           uint32_t *pSource, unsigned *pnPayload);

unsigned NetArpBuild (uint8_t *pOut, const uint8_t *pFrom, uint32_t nFrom, uint16_t nOp,
                      const uint8_t *pTo, const uint8_t *pTargetMAC, uint32_t nTarget);

// "192.168.2.1" into host order; false for anything else.
bool NetParseAddress (const char *pText, uint32_t *pAddress);

// Whether the frame is an ARP reply from nWho; its MAC into pMAC when it is.
bool NetArpAnswer (const uint8_t *pFrame, unsigned nLength, uint32_t nWho, uint8_t *pMAC);

#endif
