/*
 * wlan_hostap.c — what the boot menu asks of the WPA supplicant.
 *
 * The networks in range with their signal, whether the one asked for is
 * joined, a scan on demand and a new configuration without a restart. All of
 * it lives inside struct wpa_supplicant, whose layout depends on the CONFIG_*
 * switches the library was built with — so this is C, compiled with exactly
 * the switches of addon/wlan/hostap/wpa_supplicant/Makefile.circle
 * (src/kernel/Makefile, WLAN_HOSTAP_FLAGS). Read from C++ with other switches,
 * every field after the first conditional one would be read from the wrong
 * place.
 *
 * The supplicant runs as a task of Circle's cooperative scheduler, and so does
 * whoever calls this: nothing here can be interrupted half-way by it.
 *
 * Circle's main_circle.c keeps its interface to itself (a static `global`), so
 * the pointer is taken where it is made: wpa_supplicant_add_iface() is wrapped
 * on the final link (FINAL_WRAPS).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Qualified, because this directory has a config.h of its own and a quoted
// name is looked for beside the including file first.
#include "utils/includes.h"

#include "utils/common.h"
#include "utils/list.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "wpa_supplicant/config.h"
#include "wpa_supplicant/wpa_supplicant_i.h"
#include "wpa_supplicant/bss.h"
#include "wpa_supplicant/scan.h"

#include "wlan_hostap.h"

static struct wpa_supplicant *s_wpa_s;

struct wpa_supplicant *__real_wpa_supplicant_add_iface (struct wpa_global *global,
                                                        struct wpa_interface *iface,
                                                        struct wpa_supplicant *parent);

struct wpa_supplicant *__wrap_wpa_supplicant_add_iface (struct wpa_global *global,
                                                        struct wpa_interface *iface,
                                                        struct wpa_supplicant *parent)
{
	struct wpa_supplicant *wpa_s = __real_wpa_supplicant_add_iface (global, iface, parent);
	if (wpa_s != NULL && s_wpa_s == NULL)
	{
		s_wpa_s = wpa_s;
	}
	return wpa_s;
}

int okapia_wlan_ready (void)
{
	return s_wpa_s != NULL;
}

int okapia_wlan_scan (void)
{
	if (s_wpa_s == NULL)
	{
		return 0;
	}
	// A manual request, which the supplicant honours with no network enabled
	// — the case of a card that has not been told one yet.
	s_wpa_s->scan_req = MANUAL_SCAN_REQ;
	wpa_supplicant_req_scan (s_wpa_s, 0, 0);
	return 1;
}

unsigned okapia_wlan_seen (struct okapia_wlan_bss *out, unsigned max)
{
	unsigned n = 0;
	struct wpa_bss *bss;

	if (s_wpa_s == NULL)
	{
		return 0;
	}
	dl_list_for_each (bss, &s_wpa_s->bss, struct wpa_bss, list)
	{
		size_t len;

		if (n >= max)
		{
			break;
		}
		len = bss->ssid_len < OKAPIA_WLAN_SSID - 1 ? bss->ssid_len : OKAPIA_WLAN_SSID - 1;
		os_memcpy (out[n].ssid, bss->ssid, len);
		out[n].ssid[len] = '\0';
		// A name with a NUL in it cannot be shown or typed back; it is a
		// hidden network as far as anybody choosing is concerned.
		if (os_strlen (out[n].ssid) != len)
		{
			out[n].ssid[0] = '\0';
		}
		out[n].dbm = bss->level;
		out[n].secured = (bss->caps & IEEE80211_CAP_PRIVACY) != 0;
		n++;
	}
	return n;
}

int okapia_wlan_reload (void)
{
	if (s_wpa_s == NULL)
	{
		return 0;
	}
	return wpa_supplicant_reload_configuration (s_wpa_s) == 0;
}

int okapia_wlan_state (char *ssid, unsigned size, int *dbm)
{
	struct wpa_ssid *wanted;

	if (ssid != NULL && size != 0)
	{
		ssid[0] = '\0';
	}
	if (dbm != NULL)
	{
		*dbm = 0;
	}
	if (s_wpa_s == NULL)
	{
		return OKAPIA_WLAN_NONE;
	}

	if (s_wpa_s->wpa_state == WPA_COMPLETED)
	{
		if (s_wpa_s->current_bss != NULL)
		{
			if (ssid != NULL && size != 0)
			{
				size_t len = s_wpa_s->current_bss->ssid_len < size - 1
					   ? s_wpa_s->current_bss->ssid_len : size - 1;
				os_memcpy (ssid, s_wpa_s->current_bss->ssid, len);
				ssid[len] = '\0';
			}
			if (dbm != NULL)
			{
				*dbm = s_wpa_s->current_bss->level;
			}
		}
		return OKAPIA_WLAN_CONNECTED;
	}

	// The one network the configuration names, if any: its failures say the
	// key was refused, which is the one thing worth telling apart from "not
	// yet".
	wanted = s_wpa_s->conf != NULL ? s_wpa_s->conf->ssid : NULL;
	if (wanted != NULL && ssid != NULL && size != 0)
	{
		size_t len = wanted->ssid_len < size - 1 ? wanted->ssid_len : size - 1;
		os_memcpy (ssid, wanted->ssid, len);
		ssid[len] = '\0';
	}
	if (wanted != NULL && wanted->auth_failures > 0)
	{
		return OKAPIA_WLAN_REFUSED;
	}

	switch (s_wpa_s->wpa_state)
	{
	case WPA_AUTHENTICATING:
	case WPA_ASSOCIATING:
	case WPA_ASSOCIATED:
	case WPA_4WAY_HANDSHAKE:
	case WPA_GROUP_HANDSHAKE:
		return OKAPIA_WLAN_JOINING;

	case WPA_SCANNING:
		return wanted != NULL ? OKAPIA_WLAN_JOINING : OKAPIA_WLAN_SEARCHING;

	default:
		break;
	}
	if (s_wpa_s->scanning)
	{
		return wanted != NULL ? OKAPIA_WLAN_JOINING : OKAPIA_WLAN_SEARCHING;
	}
	return wanted != NULL ? OKAPIA_WLAN_LOST : OKAPIA_WLAN_IDLE;
}
