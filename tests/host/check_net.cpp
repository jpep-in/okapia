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

#include "net_config_circle.h"

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

int main (void)
{
    CheckEtherKind ();
    CheckCopy ();
    CheckConfig ();

    if (s_nFailures != 0)
    {
        printf ("%u failure(s)\n", s_nFailures);
        return 1;
    }
    printf ("all network checks passed\n");
    return 0;
}
