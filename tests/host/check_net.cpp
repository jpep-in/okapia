/*
 * check_net.cpp — the network preferences and what the supplicant is told.
 *
 * A Wi-Fi configuration the supplicant reads wrong does not fail on a board:
 * the Macintosh simply never finds its router, with nothing on the screen to
 * say why. Everything that decides it — which `ether` values mean a network,
 * what an editor leaves at the end of a value, how a passphrase full of
 * quotes and hashes reaches the supplicant — is decidable here.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "net_config_okapia.h"
#include "net_clock.h"

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

// Stands in for PBKDF2: records what it was given and answers a pattern, so
// the test sees exactly which bytes would have been hashed.
static char          s_Passphrase[128];
static unsigned char s_SSID[64];
static size_t        s_nSSID;
static unsigned      s_nDerived;

static bool FakeDerive (const char *pPassphrase, const unsigned char *pSSID,
                        size_t nSSIDLength, unsigned char *pPSK)
{
    s_nDerived++;
    strncpy (s_Passphrase, pPassphrase, sizeof s_Passphrase - 1);
    memcpy (s_SSID, pSSID, nSSIDLength);
    s_nSSID = nSSIDLength;
    for (unsigned i = 0; i < 32; i++)
    {
        pPSK[i] = (unsigned char) (0xa0 + i);
    }
    return true;
}

static const char FAKE_PSK_HEX[] =
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebf";

static bool Contains (const char *pText, const char *pWhat)
{
    return strstr (pText, pWhat) != 0;
}

static void CheckEtherKind (void)
{
    printf ("ether\n");
    Expect (NetEtherKindOf ("eth") == NetEtherWired, "eth is the wired interface");
    Expect (NetEtherKindOf ("WLAN") == NetEtherWLAN, "case does not matter");
    Expect (NetEtherKindOf ("wlan\r") == NetEtherWLAN, "a Windows line ending is not part of it");
    Expect (NetEtherKindOf ("wlan  ") == NetEtherWLAN, "nor are trailing blanks");
    Expect (NetEtherKindOf ("slirp") == NetEtherNone, "a desktop Basilisk II's slirp means no network here");
    Expect (NetEtherKindOf ("ethx") == NetEtherNone, "a prefix is not a match");
    Expect (NetEtherKindOf ("") == NetEtherNone, "empty is no network");
    Expect (NetEtherKindOf (0) == NetEtherNone, "absent is no network");
}

static void CheckCopy (void)
{
    printf ("values\n");
    char Out[16];
    NetCopyValue (Out, sizeof Out, "My net\r");
    Expect (strcmp (Out, "My net") == 0, "the '\\r' an editor adds is dropped");
    NetCopyValue (Out, sizeof Out, " spaced ");
    Expect (strcmp (Out, " spaced ") == 0, "spaces at either end are kept: they are legal");
    NetCopyValue (Out, sizeof Out, "0123456789abcdefXYZ");
    Expect (strcmp (Out, "0123456789abcde") == 0, "a value too long is cut, and terminated");
    NetCopyValue (Out, sizeof Out, 0);
    Expect (Out[0] == '\0', "an absent value is empty");
}

static void CheckConfig (void)
{
    printf ("supplicant configuration\n");
    char Out[512];
    const char *pError;

    Expect (NetWpaConfigBuild (Out, sizeof Out, "My net", "", "FR", FakeDerive, &pError),
            "an open network is accepted");
    Expect (Contains (Out, "country=FR\n"), "the country comes first");
    Expect (Contains (Out, "\tssid=4d79206e6574\n"), "the SSID is written in hex");
    Expect (Contains (Out, "\tkey_mgmt=NONE\n") && !Contains (Out, "psk="),
            "an empty password is an open network, with no key");

    s_nDerived = 0;
    const char Tricky[] = "a \"b\" #c; d";
    Expect (NetWpaConfigBuild (Out, sizeof Out, "Caf\xc3\xa9 #2", Tricky, "DE", FakeDerive, &pError),
            "quotes, a hash and a semicolon in the passphrase are accepted");
    Expect (s_nDerived == 1 && strcmp (s_Passphrase, Tricky) == 0,
            "the passphrase is hashed exactly as written");
    Expect (s_nSSID == 8 && memcmp (s_SSID, "Caf\xc3\xa9 #2", 8) == 0,
            "with the SSID's bytes as the salt");
    Expect (Contains (Out, "\tpsk=") && Contains (Out, FAKE_PSK_HEX),
            "the supplicant receives the key, never the passphrase");
    Expect (!Contains (Out, "#") && !Contains (Out, "\""),
            "nothing in the file can be cut as a comment or misquoted");
    Expect (Contains (Out, "\tssid=436166c3a9202332\n"), "a UTF-8 SSID survives in hex");
    Expect (Contains (Out, "\tkey_mgmt=WPA-PSK\n"), "a password means WPA-PSK");

    const char HexKey[] = "00112233445566778899AABBCCDDEEFF00112233445566778899aabbccddeeff";
    s_nDerived = 0;
    Expect (NetWpaConfigBuild (Out, sizeof Out, "x", HexKey, "US", FakeDerive, &pError)
            && Contains (Out, HexKey) && s_nDerived == 0,
            "64 hex digits are the key itself, not a passphrase");

    char Long[70];
    memset (Long, 'p', 63);
    Long[63] = '\0';
    Expect (NetWpaConfigBuild (Out, sizeof Out, "x", Long, "US", FakeDerive, &pError),
            "63 characters is the longest passphrase");
    memset (Long, 'g', 64);
    Long[64] = '\0';
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "x", Long, "US", FakeDerive, &pError),
            "64 characters that are not hex are refused");
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "x", "1234567", "US", FakeDerive, &pError)
            && pError != 0 && !Contains (pError, "1234567"),
            "seven characters are refused, and the message does not repeat them");
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "x", "mot de pass\xc3\xa9", "FR", FakeDerive, &pError),
            "a passphrase outside printable ASCII is refused");

    Expect (!NetWpaConfigBuild (Out, sizeof Out, "", "", "FR", FakeDerive, &pError),
            "an empty SSID is refused");
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "123456789012345678901234567890123", "", "FR",
                                FakeDerive, &pError),
            "33 bytes of SSID are refused");
    Expect (NetWpaConfigBuild (Out, sizeof Out, "12345678901234567890123456789012", "", "FR",
                               FakeDerive, &pError),
            "32 bytes of SSID are accepted");
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "x", "", "fr", FakeDerive, &pError),
            "the country is capitals: the caller converts it");
    Expect (!NetWpaConfigBuild (Out, sizeof Out, "x", "", "FRA", FakeDerive, &pError),
            "and two of them");

    char Small[40];
    Expect (!NetWpaConfigBuild (Small, sizeof Small, "My net", "", "FR", FakeDerive, &pError)
            && Small[0] == '\0',
            "a buffer too small is an error, never a truncated file");
}

// The configuration the boot menu starts the Wi-Fi with before a network has
// been chosen: the supplicant has to run to answer a scan.
static void CheckNoNetwork (void)
{
    char Out[64];
    const char *pError = 0;
    Expect (NetWpaConfigNoNetwork (Out, sizeof Out, "FR", &pError),
            "a country alone is a configuration");
    Expect (strcmp (Out, "country=FR\n") == 0, "and it is only the country");
    Expect (!Contains (Out, "network="), "with no network block to join");
    Expect (!NetWpaConfigNoNetwork (Out, sizeof Out, "fr", &pError) && pError != 0,
            "a country must still be two capitals");
    char Tiny[8];
    Expect (!NetWpaConfigNoNetwork (Tiny, sizeof Tiny, "FR", &pError) && Tiny[0] == '\0',
            "a buffer too small is refused, and left empty");
}

/*
 *  The clock's question, against a fake router
 *
 *  192.168.1.1 is the router, the DHCP server and the name server, as in any
 *  home; the time server is beyond it. Every frame the router sends is built
 *  here by hand, so a field read at the wrong offset is a failed check and not
 *  a Macintosh whose clock is silently an hour out.
 */
static const uint8_t OUR_MAC[6]    = { 0xB8, 0x27, 0xEB, 0x11, 0x22, 0x33 };
static const uint8_t ROUTER_MAC[6] = { 0x00, 0x24, 0xD4, 0xAA, 0xBB, 0xCC };
static const uint32_t ROUTER   = 0xC0A80101;     // 192.168.1.1
static const uint32_t OFFERED  = 0xC0A80132;     // 192.168.1.50
static const uint32_t TIME_A   = 0xCB007105;     // 203.0.113.5
static const uint32_t TIME_B   = 0xCB007106;     // 203.0.113.6

static uint16_t G16 (const uint8_t *p) { return (uint16_t) (p[0] << 8 | p[1]); }
static uint32_t G32 (const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static void P16 (uint8_t *p, uint16_t n) { p[0] = (uint8_t) (n >> 8); p[1] = (uint8_t) n; }
static void P32 (uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t) (n >> 24); p[1] = (uint8_t) (n >> 16); p[2] = (uint8_t) (n >> 8); p[3] = (uint8_t) n;
}

static uint32_t Sum16 (uint32_t nSum, const uint8_t *p, unsigned n)
{
    for (unsigned i = 0; i + 1 < n; i += 2) nSum += G16 (p + i);
    if (n & 1) nSum += (uint32_t) p[n - 1] << 8;
    return nSum;
}

static bool ChecksumsHold (const uint8_t *f, unsigned n)
{
    const uint8_t *ip = f + 14;
    uint32_t s = Sum16 (0, ip, 20);
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    if (s != 0xFFFF) return false;
    const unsigned nUdp = G16 (ip + 20 + 4);
    uint32_t u = Sum16 (0, ip + 12, 8) + 17 + nUdp;
    u = Sum16 (u, ip + 20, nUdp);
    while (u >> 16) u = (u & 0xFFFF) + (u >> 16);
    return u == 0xFFFF && 14 + 20 + nUdp <= n;
}

// A UDP frame from the router's MAC, payload already at offset 42.
static unsigned Udp (uint8_t *f, uint32_t nFrom, uint32_t nTo, uint16_t nSport, uint16_t nDport,
                     unsigned nPayload)
{
    memcpy (f, OUR_MAC, 6);
    memcpy (f + 6, ROUTER_MAC, 6);
    P16 (f + 12, 0x0800);
    uint8_t *ip = f + 14;
    memset (ip, 0, 20);
    ip[0] = 0x45; ip[8] = 64; ip[9] = 17;
    P16 (ip + 2, (uint16_t) (28 + nPayload));
    P32 (ip + 12, nFrom);
    P32 (ip + 16, nTo);
    P16 (ip + 20, nSport);
    P16 (ip + 22, nDport);
    P16 (ip + 24, (uint16_t) (8 + nPayload));
    P16 (ip + 26, 0);
    return 42 + nPayload;
}

static unsigned DhcpReply (uint8_t *f, uint32_t nXid, uint8_t nType, const uint8_t *pChaddr)
{
    uint8_t *d = f + 42;
    memset (d, 0, 300);
    d[0] = 2; d[1] = 1; d[2] = 6;
    P32 (d + 4, nXid);
    P32 (d + 16, OFFERED);
    memcpy (d + 28, pChaddr, 6);
    d[236] = 99; d[237] = 130; d[238] = 83; d[239] = 99;
    unsigned i = 240;
    d[i++] = 53; d[i++] = 1; d[i++] = nType;
    d[i++] = 54; d[i++] = 4; P32 (d + i, ROUTER); i += 4;
    d[i++] = 1;  d[i++] = 4; P32 (d + i, 0xFFFFFF00); i += 4;
    d[i++] = 3;  d[i++] = 4; P32 (d + i, ROUTER); i += 4;
    d[i++] = 6;  d[i++] = 4; P32 (d + i, ROUTER); i += 4;
    d[i++] = 255;
    return Udp (f, ROUTER, 0xFFFFFFFF, 67, 68, 300);
}

static unsigned ArpFrom (uint8_t *f, uint16_t nOp, uint32_t nSender, uint32_t nTarget)
{
    memcpy (f, nOp == 1 ? (const uint8_t *) "\xff\xff\xff\xff\xff\xff" : OUR_MAC, 6);
    memcpy (f + 6, ROUTER_MAC, 6);
    P16 (f + 12, 0x0806);
    uint8_t *a = f + 14;
    P16 (a, 1); P16 (a + 2, 0x0800); a[4] = 6; a[5] = 4; P16 (a + 6, nOp);
    memcpy (a + 8, ROUTER_MAC, 6);
    P32 (a + 14, nSender);
    memcpy (a + 18, nOp == 1 ? (const uint8_t *) "\0\0\0\0\0\0" : OUR_MAC, 6);
    P32 (a + 24, nTarget);
    return 42;
}

// The answer for pool.ntp.org: a CNAME first, then two A records, all names
// after the question compressed — what a home router's resolver sends.
static unsigned DnsAnswer (uint8_t *f, const uint8_t *pQuery, unsigned nQuery, uint16_t nPort)
{
    uint8_t *d = f + 42;
    memcpy (d, pQuery, nQuery);
    d[2] = 0x81; d[3] = 0x80;
    P16 (d + 6, 3);
    unsigned i = nQuery;
    d[i++] = 0xC0; d[i++] = 12; P16 (d + i, 5); P16 (d + i + 2, 1); P32 (d + i + 4, 60);
    P16 (d + i + 8, 4); i += 10;
    d[i++] = 1; d[i++] = 'x'; d[i++] = 0xC0; d[i++] = 12;          // x.pool.ntp.org
    d[i++] = 0xC0; d[i++] = (uint8_t) nQuery; P16 (d + i, 1); P16 (d + i + 2, 1);
    P32 (d + i + 4, 60); P16 (d + i + 8, 4); i += 10; P32 (d + i, TIME_A); i += 4;
    d[i++] = 0xC0; d[i++] = (uint8_t) nQuery; P16 (d + i, 1); P16 (d + i + 2, 1);
    P32 (d + i + 4, 60); P16 (d + i + 8, 4); i += 10; P32 (d + i, TIME_B); i += 4;
    return Udp (f, ROUTER, OFFERED, 53, nPort, i);
}

static unsigned NtpAnswer (uint8_t *f, uint32_t nFrom, const uint8_t *pNonce, uint16_t nPort,
                           uint8_t nStratum, uint32_t nSeconds, uint32_t nFraction)
{
    uint8_t *d = f + 42;
    memset (d, 0, 48);
    d[0] = 0x24;                        // version 4, server
    d[1] = nStratum;
    memcpy (d + 24, pNonce, 8);
    P32 (d + 40, nSeconds);
    P32 (d + 44, nFraction);
    return Udp (f, nFrom, OFFERED, 123, nPort, 48);
}

static void CheckClock (void)
{
    printf ("\nthe clock's question\n");
    static TNetClock C;
    uint8_t Out[NET_CLOCK_FRAME], In[1514];
    unsigned nOut = 0;

    NetClockBegin (&C, OUR_MAC, "pool.ntp.org", 12345, 1000);
    NetClockStep (&C, 1000, Out, &nOut);
    const uint8_t *d = Out + 42;
    Expect (nOut == 42 + 300 && memcmp (Out, "\xff\xff\xff\xff\xff\xff", 6) == 0
            && memcmp (Out + 6, OUR_MAC, 6) == 0 && G16 (Out + 36) == 67 && d[242] == 1,
            "a DHCPDISCOVER, broadcast, from our MAC, 300 bytes of BOOTP");
    Expect (ChecksumsHold (Out, nOut), "its IP and UDP checksums hold");
    Expect ((G16 (d + 10) & 0x8000) != 0, "it asks for a broadcast answer");
    const uint32_t nXid = G32 (d + 4);

    // Somebody else's DHCP — the Macintosh's own — is left to it.
    uint8_t Other[6] = { 1, 2, 3, 4, 5, 6 };
    unsigned n = DhcpReply (In, nXid + 1, 2, OUR_MAC);
    Expect (!NetClockTake (&C, In, n, 1010, Out, &nOut) && nOut == 0,
            "an offer for another transaction goes on to the Macintosh");
    n = DhcpReply (In, nXid, 2, Other);
    Expect (!NetClockTake (&C, In, n, 1010, Out, &nOut), "and so does one for another MAC");

    n = DhcpReply (In, nXid, 2, OUR_MAC);
    Expect (NetClockTake (&C, In, n, 1020, Out, &nOut) && C.State == NetClockRequest,
            "the offer is ours");
    bool bRequested = false, bServer = false;
    for (unsigned i = 42 + 240; i + 6 <= nOut && Out[i] != 255; i += 2 + Out[i + 1])
    {
        bRequested = bRequested || (Out[i] == 50 && G32 (Out + i + 2) == OFFERED);
        bServer    = bServer    || (Out[i] == 54 && G32 (Out + i + 2) == ROUTER);
    }
    Expect (nOut != 0 && Out[42 + 242] == 3 && bRequested && bServer,
            "and is answered with a DHCPREQUEST naming the address and the server");

    n = DhcpReply (In, nXid, 5, OUR_MAC);
    Expect (NetClockTake (&C, In, n, 1030, Out, &nOut) && C.nAddress == OFFERED
            && C.nRouter == ROUTER && C.nDnsCount == 1, "the ack gives an address, a router, a name server");
    Expect (nOut == 42 && G16 (Out + 12) == 0x0806 && G16 (Out + 20) == 1
            && G32 (Out + 28) == OFFERED && G32 (Out + 38) == ROUTER,
            "then ARP asks for the router, from our new address");

    n = ArpFrom (In, 2, ROUTER, OFFERED);
    Expect (!NetClockTake (&C, In, n, 1040, Out, &nOut), "the ARP reply also reaches the Macintosh");
    Expect (C.State == NetClockLookUp && nOut != 0 && memcmp (Out, ROUTER_MAC, 6) == 0
            && G16 (Out + 36) == 53 && G32 (Out + 30) == ROUTER,
            "and the name is asked of the router");
    Expect (ChecksumsHold (Out, nOut), "its checksums hold");
    Expect (memcmp (Out + 42 + 12, "\x04pool\x03ntp\x03org\x00", 14) == 0,
            "pool.ntp.org, in labels");
    const uint16_t nPort = G16 (Out + 34);
    uint8_t Query[256];
    const unsigned nQuery = nOut - 42;
    memcpy (Query, Out + 42, nQuery);

    n = DnsAnswer (In, Query, nQuery, (uint16_t) (nPort + 1));
    Expect (!NetClockTake (&C, In, n, 1050, Out, &nOut), "an answer to another port is not ours");
    n = DnsAnswer (In, Query, nQuery, nPort);
    Expect (NetClockTake (&C, In, n, 1060, Out, &nOut) && C.nServers == 2
            && C.Servers[0] == TIME_A && C.Servers[1] == TIME_B,
            "the answer gives both addresses, past a CNAME and compressed names");
    Expect (C.State == NetClockAsk && nOut == 42 + 48 && G32 (Out + 30) == TIME_A
            && memcmp (Out, ROUTER_MAC, 6) == 0 && Out[42] == 0x23,
            "and the first is asked the time, through the router");
    Expect (ChecksumsHold (Out, nOut), "its checksums hold");
    uint8_t Nonce[8];
    memcpy (Nonce, Out + 42 + 40, 8);

    n = ArpFrom (In, 1, ROUTER, OFFERED);
    Expect (!NetClockTake (&C, In, n, 1070, Out, &nOut) && nOut == 42 && G16 (Out + 20) == 2
            && memcmp (Out, ROUTER_MAC, 6) == 0 && G32 (Out + 28) == OFFERED,
            "the router asking who has our address is answered, and the Macintosh hears it too");

    const uint32_t nWhen = 3972036000u;  // 2025-11-13 ... any era-0 time
    uint8_t Wrong[8] = { 0 };
    n = NtpAnswer (In, TIME_A, Wrong, nPort, 2, nWhen, 0);
    Expect (NetClockTake (&C, In, n, 1080, Out, &nOut) && C.State == NetClockAsk,
            "an answer to another question is taken and ignored");
    n = NtpAnswer (In, TIME_B, Nonce, nPort, 2, nWhen, 0);
    NetClockTake (&C, In, n, 1080, Out, &nOut);
    Expect (C.State == NetClockAsk, "and so is one from a server not asked");
    n = NtpAnswer (In, TIME_A, Nonce, nPort, 2, nWhen, 0x80000000u);
    Expect (NetClockTake (&C, In, n, 1100, Out, &nOut) && C.State == NetClockDone,
            "the answer is ours");
    // Sent at 1060, back at 1100: half the trip is 20 ms, on top of half a second.
    Expect (C.nUnix == NetClockUnixOf (nWhen) && C.nMillis == 520 && C.nAt == 1100
            && C.nStratum == 2 && C.nFrom == TIME_A, "the time, plus half the round trip");

    // No answer at all: four discoveries two seconds apart, then the failure.
    NetClockBegin (&C, OUR_MAC, "pool.ntp.org", 99, 0);
    unsigned nSent = 0;
    for (unsigned t = 0; t <= 10000; t += 100)
    {
        NetClockStep (&C, t, Out, &nOut);
        nSent += nOut != 0;
    }
    Expect (nSent == 4 && C.State == NetClockFailed && C.Failure == NetClockNoLease,
            "no DHCP server: four tries, then a failure that says so");

    // A kiss of death moves on to the next address; a dotted server skips DNS.
    NetClockBegin (&C, OUR_MAC, "192.168.1.9", 7, 0);
    NetClockStep (&C, 0, Out, &nOut);
    const uint32_t nXid2 = G32 (Out + 42 + 4);
    n = DhcpReply (In, nXid2, 2, OUR_MAC);
    NetClockTake (&C, In, n, 10, Out, &nOut);
    n = DhcpReply (In, nXid2, 5, OUR_MAC);
    NetClockTake (&C, In, n, 20, Out, &nOut);
    Expect (nOut == 42 && G32 (Out + 38) == 0xC0A80109, "a server on the same network is asked for directly");
    Expect (!NetClockTake (&C, In, ArpFrom (In, 2, ROUTER, OFFERED), 30, Out, &nOut) && nOut == 0
            && C.State == NetClockResolveRouter, "the router answering is not the server answering");

    NetClockBegin (&C, OUR_MAC, "bad name!", 7, 0);
    Expect (C.State == NetClockFailed && C.Failure == NetClockNoName, "a name DNS cannot carry fails at once");

    Expect (NetClockUnixOf (16) == 2085978512u, "NTP era 1 begins on 7 February 2036, 06:28:16 UTC");
    Expect (NetClockUnixOf (2208988800u) == 0, "and era 0 at 1970");
    char Address[16];
    Expect (strcmp (NetClockAddress (TIME_A, Address), "203.0.113.5") == 0, "addresses are said dotted");

    // The frames the network bench reuses.
    uint8_t F[1514];
    const unsigned nF = NetUdpBuild (F, OUR_MAC, ROUTER_MAC, OFFERED, ROUTER, 9001, 9000, 1472, 0, false);
    uint32_t nFrom = 0;
    unsigned nPay = 0;
    Expect (nF == 1514 && G16 (F + 40) == 0 && NetUdpFind (F, nF, 9001, 9000, &nFrom, &nPay) == F + 42
            && nFrom == OFFERED && nPay == 1472,
            "a full-size UDP frame without a checksum, found again by its ports");
    uint8_t Who[6] = { 0 };
    Expect (NetArpAnswer (In, ArpFrom (In, 2, ROUTER, OFFERED), ROUTER, Who)
            && memcmp (Who, ROUTER_MAC, 6) == 0
            && !NetArpAnswer (In, ArpFrom (In, 1, ROUTER, OFFERED), ROUTER, Who),
            "an ARP reply gives the MAC; a request does not");
    uint32_t nParsed = 0;
    Expect (NetParseAddress ("192.168.2.1", &nParsed) && nParsed == 0xC0A80201
            && !NetParseAddress ("192.168.2", &nParsed) && !NetParseAddress ("pool.ntp.org", &nParsed),
            "an address is parsed, anything else refused");
}

int main (void)
{
    CheckEtherKind ();
    CheckCopy ();
    CheckConfig ();
    CheckNoNetwork ();
    CheckClock ();

    if (s_nFailures != 0)
    {
        printf ("%u failure(s)\n", s_nFailures);
        return 1;
    }
    printf ("all network checks passed\n");
    return 0;
}
