/*
 * wlan_circle.h — the on-board Wi-Fi and its WPA supplicant, for the board.
 *
 * Used by board_net_circle.cpp only. Circle's WLAN driver and its port of
 * wpa_supplicant are Circle tasks: this brings up the scheduler they need,
 * loads the Broadcom firmware from /firmware on the card, and hands the
 * supplicant the network the preferences name — from memory, never from a
 * file written on the card.
 *
 * Circle's supplicant expects Circle's TCP/IP stack under it, for its EAPOL
 * frames alone. Okapia runs no stack — every other frame is the Macintosh's —
 * so wlan_circle.cpp defines the supplicant's l2_packet layer itself, and the
 * board routes the handshake frames here (WlanTakeEAPOL).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_WLAN_CIRCLE_H
#define OKAPIA_WLAN_CIRCLE_H

#include <circle/netdevice.h>
#include <circle/types.h>

#include "wlan_hostap.h"

// The handshake's ethertype. Frames of this type go to WlanTakeEAPOL().
static const u16 WLAN_ETHERTYPE_EAPOL = 0x888E;

// Once: false, with the reason in the log, when there is no Wi-Fi to start
// (QEMU, missing firmware, a preference the supplicant would reject).
// pCountry is two capital letters.
CNetDevice *WlanStart (const char *pSSID, const char *pPassword, const char *pCountry);

// Lets the driver's and the supplicant's tasks run. Only from the emulation
// thread's own seams, never from an interrupt.
// Whether this build can have the on-board Wi-Fi at all: not under QEMU, and
// not when the SD card sits on the controller the Wi-Fi needs.
bool WlanAvailable (void);

// A new network for the supplicant that is already running: the boot menu's
// Join. An empty SSID leaves it joined to nothing, scanning on demand.
bool WlanApply (const char *pSSID, const char *pPassword, const char *pCountry);

// A scan now; what the last ones found; and how the joining goes (one of the
// OKAPIA_WLAN_* values, wlan_hostap.h).
bool     WlanScan (void);
unsigned WlanSeen (okapia_wlan_bss *pOut, unsigned nMax);
int      WlanState (char *pSSID, unsigned nSize, int *pDbm);

void WlanYield (void);

// A frame of type WLAN_ETHERTYPE_EAPOL, header included. Copied; dropped when
// the supplicant has not taken the previous ones yet.
void WlanTakeEAPOL (const u8 *pFrame, unsigned nLength);

// The network's name, for the log.
const char *WlanSSID (void);

#endif
