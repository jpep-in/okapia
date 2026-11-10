/*
 * net_config_circle.h — the network preferences, read and checked.
 *
 * Pure: no Circle, no emulator, so tests/host checks it on the development
 * machine. What it decides is what a mistake would cost most to find on a
 * board: whether `ether` asks for a network at all, and what the Wi-Fi
 * supplicant is told — a configuration it would read wrong fails with nothing
 * on the screen but a Macintosh that never finds its router.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_NET_CONFIG_CIRCLE_H
#define OKAPIA_NET_CONFIG_CIRCLE_H

#include <stddef.h>

// The `ether` preference: "eth" or "wlan", in any case; anything else, or
// nothing, is no network.
enum TNetEtherKind
{
    NetEtherNone,
    NetEtherWired,
    NetEtherWLAN
};

TNetEtherKind NetEtherKindOf (const char *pValue);

// Copies a preference value without what a text editor may have added to it:
// the '\r' of a file saved with Windows line endings. Spaces are kept — they
// are legal in an SSID and in a passphrase, inside and at either end.
void NetCopyValue (char *pOut, size_t nSize, const char *pValue);

// WPA's pre-shared key from a passphrase: PBKDF2-HMAC-SHA1, 4096 rounds, the
// SSID as salt, 32 bytes (IEEE 802.11i, H.4). Given rather than linked so the
// host test needs no supplicant.
typedef bool TNetPskDerive (const char *pPassphrase,
                            const unsigned char *pSSID, size_t nSSIDLength,
                            unsigned char *pPSK);

// The supplicant's configuration for one network, written into pOut.
//
// - pSSID: 1 to 32 bytes, written in hex so no character can be misread.
// - pPassword: empty for an open network; 64 hex digits for a key given as
//   such; otherwise a passphrase of 8 to 63 printable ASCII characters,
//   turned into the key here. The supplicant's own parser is never handed the
//   passphrase: it cuts a line at a '#' that a '"' earlier in it fooled.
// - pCountry: two letters, already chosen by the caller.
//
// Answers false with *ppError naming what was wrong; the password itself is
// never part of the message.
bool NetWpaConfigBuild (char *pOut, size_t nSize,
                        const char *pSSID, const char *pPassword,
                        const char *pCountry, TNetPskDerive *pDerive,
                        const char **ppError);

#endif
