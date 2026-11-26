/*
 * net_config_okapia.cpp — the network preferences, read and checked.
 *
 * See net_config_okapia.h. Nothing here may name Circle or the emulator:
 * tests/host/check_net.cpp links this file on the development machine.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "net_config_okapia.h"

#include <stdio.h>
#include <string.h>

static bool SameWord (const char *pValue, const char *pWord)
{
    for (; *pWord != '\0'; pValue++, pWord++)
    {
        char c = *pValue;
        if (c >= 'A' && c <= 'Z')
        {
            c = (char) (c - 'A' + 'a');
        }
        if (c != *pWord)
        {
            return false;
        }
    }
    // Trailing blanks and a '\r' are what an editor leaves; nothing else is.
    while (*pValue == ' ' || *pValue == '\t' || *pValue == '\r')
    {
        pValue++;
    }
    return *pValue == '\0';
}

TNetEtherKind NetEtherKindOf (const char *pValue)
{
    if (pValue == 0)
    {
        return NetEtherNone;
    }
    if (SameWord (pValue, "eth"))
    {
        return NetEtherWired;
    }
    if (SameWord (pValue, "wlan"))
    {
        return NetEtherWLAN;
    }
    return NetEtherNone;
}

void NetCopyValue (char *pOut, size_t nSize, const char *pValue)
{
    if (nSize == 0)
    {
        return;
    }
    pOut[0] = '\0';
    if (pValue == 0)
    {
        return;
    }
    size_t nLength = strlen (pValue);
    while (nLength > 0 && (pValue[nLength - 1] == '\r' || pValue[nLength - 1] == '\n'))
    {
        nLength--;
    }
    if (nLength >= nSize)
    {
        nLength = nSize - 1;
    }
    memcpy (pOut, pValue, nLength);
    pOut[nLength] = '\0';
}

static bool IsHexDigit (char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static bool IsHexKey (const char *p)
{
    if (strlen (p) != 64)
    {
        return false;
    }
    for (; *p != '\0'; p++)
    {
        if (!IsHexDigit (*p))
        {
            return false;
        }
    }
    return true;
}

// Appends to a bounded buffer; remembers an overflow rather than truncating.
struct TOut
{
    char  *p;
    size_t nSize, nUsed;
    bool   bFull;
};

static void Put (TOut *pOut, const char *pText)
{
    const size_t n = strlen (pText);
    if (pOut->bFull || pOut->nUsed + n >= pOut->nSize)
    {
        pOut->bFull = true;
        return;
    }
    memcpy (pOut->p + pOut->nUsed, pText, n + 1);
    pOut->nUsed += n;
}

static void PutHex (TOut *pOut, const unsigned char *p, size_t n)
{
    static const char Digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        const char Pair[3] = { Digits[p[i] >> 4], Digits[p[i] & 15], '\0' };
        Put (pOut, Pair);
    }
}

bool NetWpaConfigBuild (char *pOut, size_t nSize,
                        const char *pSSID, const char *pPassword,
                        const char *pCountry, TNetPskDerive *pDerive,
                        const char **ppError)
{
    const char *pDummy;
    if (ppError == 0)
    {
        ppError = &pDummy;
    }
    *ppError = 0;

    const size_t nSSID = pSSID != 0 ? strlen (pSSID) : 0;
    if (nSSID == 0)
    {
        *ppError = "wifissid is empty";
        return false;
    }
    if (nSSID > 32)
    {
        *ppError = "wifissid is longer than 32 bytes";
        return false;
    }

    if (pCountry == 0 || strlen (pCountry) != 2
        || pCountry[0] < 'A' || pCountry[0] > 'Z'
        || pCountry[1] < 'A' || pCountry[1] > 'Z')
    {
        *ppError = "wificountry must be two capital letters";
        return false;
    }

    if (pPassword == 0)
    {
        pPassword = "";
    }
    const size_t nPassword = strlen (pPassword);
    const bool bOpen = nPassword == 0;
    const bool bHexKey = IsHexKey (pPassword);
    unsigned char PSK[32];
    if (!bOpen && !bHexKey)
    {
        if (nPassword < 8 || nPassword > 63)
        {
            *ppError = "wifipassword must be 8 to 63 characters, or 64 hex digits";
            return false;
        }
        for (const char *p = pPassword; *p != '\0'; p++)
        {
            if (*p < 32 || *p > 126)
            {
                *ppError = "wifipassword may only hold printable ASCII characters";
                return false;
            }
        }
        if (pDerive == 0
            || !(*pDerive) (pPassword, (const unsigned char *) pSSID, nSSID, PSK))
        {
            *ppError = "the key could not be derived from wifipassword";
            return false;
        }
    }

    TOut Out = { pOut, nSize, 0, false };
    if (nSize > 0)
    {
        pOut[0] = '\0';
    }

    Put (&Out, "country=");
    Put (&Out, pCountry);
    Put (&Out, "\n"
               "network={\n"
               "\tssid=");
    PutHex (&Out, (const unsigned char *) pSSID, nSSID);
    // A network that does not broadcast its name is found only by asking for
    // it; asking costs nothing for one that does.
    Put (&Out, "\n\tscan_ssid=1\n");
    if (bOpen)
    {
        Put (&Out, "\tkey_mgmt=NONE\n");
    }
    else
    {
        Put (&Out, "\tkey_mgmt=WPA-PSK\n\tpsk=");
        if (bHexKey)
        {
            Put (&Out, pPassword);
        }
        else
        {
            PutHex (&Out, PSK, sizeof PSK);
        }
        Put (&Out, "\n");
    }
    Put (&Out, "}\n");

    memset (PSK, 0, sizeof PSK);
    if (Out.bFull)
    {
        *ppError = "the configuration does not fit its buffer";
        if (nSize > 0)
        {
            pOut[0] = '\0';
        }
        return false;
    }
    return true;
}

bool NetWpaConfigNoNetwork (char *pOut, size_t nSize, const char *pCountry,
                            const char **ppError)
{
    const char *pDummy;
    if (ppError == 0)
    {
        ppError = &pDummy;
    }
    *ppError = 0;
    if (pCountry == 0 || strlen (pCountry) != 2
        || pCountry[0] < 'A' || pCountry[0] > 'Z'
        || pCountry[1] < 'A' || pCountry[1] > 'Z')
    {
        *ppError = "wificountry must be two capital letters";
        return false;
    }
    TOut Out = { pOut, nSize, 0, false };
    if (nSize > 0)
    {
        pOut[0] = '\0';
    }
    Put (&Out, "country=");
    Put (&Out, pCountry);
    Put (&Out, "\n");
    if (Out.bFull)
    {
        *ppError = "the configuration does not fit its buffer";
        if (nSize > 0)
        {
            pOut[0] = '\0';
        }
        return false;
    }
    return true;
}
