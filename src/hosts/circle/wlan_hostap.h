/*
 * wlan_hostap.h — the boot menu's questions to the WPA supplicant, in C.
 *
 * See wlan_hostap.c for why this is C and compiled apart: the answers live in
 * struct wpa_supplicant, whose layout only the library's own switches give.
 * Nothing here exposes a hostap type, so C++ can include it.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_WLAN_HOSTAP_H
#define OKAPIA_WLAN_HOSTAP_H

#ifdef __cplusplus
extern "C" {
#endif

#define OKAPIA_WLAN_SSID 33

struct okapia_wlan_bss
{
	char ssid[OKAPIA_WLAN_SSID];    // empty for a hidden network
	int  dbm;
	int  secured;
};

enum
{
	OKAPIA_WLAN_NONE,               // no supplicant yet
	OKAPIA_WLAN_IDLE,               // running, no network configured
	OKAPIA_WLAN_SEARCHING,
	OKAPIA_WLAN_JOINING,
	OKAPIA_WLAN_CONNECTED,
	OKAPIA_WLAN_REFUSED,            // the configured network refused the key
	OKAPIA_WLAN_LOST                // a network is configured, none joined
};

int      okapia_wlan_ready (void);
int      okapia_wlan_scan (void);
unsigned okapia_wlan_seen (struct okapia_wlan_bss *out, unsigned max);
// Reads the configuration again, through the f_open wrap (wlan_circle.cpp).
int      okapia_wlan_reload (void);
// One of the values above; the network it speaks of, and its signal when
// connected.
int      okapia_wlan_state (char *ssid, unsigned size, int *dbm);

#ifdef __cplusplus
}
#endif

#endif
