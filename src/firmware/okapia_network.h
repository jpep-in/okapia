/*
 * okapia_network.h — the page that says how the Macintosh reaches the network.
 *
 * Which connection the Macintosh shares — none, the Ethernet cable, or the
 * Wi-Fi — and for the Wi-Fi, which network and its password. It replaces
 * editing `ether`, `wifissid`, `wifipassword` and `wificountry` by hand in a
 * file on another computer, which is what BlueSCSI asks of its users and what
 * this firmware exists to spare them.
 *
 * Like every page here it knows nothing of the board: it is handed what the
 * board found (the networks in range, whether the link is up) and answers what
 * the user asked for. Starting the Wi-Fi, scanning and joining are the
 * caller's (src/firmware/circle/okapia_firmware.cpp), which is what lets the
 * page be driven by synthetic events on a development machine.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_network_h
#define _okapia_network_h

#include "okapia_widgets.h"

// 32 bytes is what 802.11 allows a network's name; 63 characters a WPA
// passphrase, or 64 hex digits for a key given as such.
static const unsigned NETWORK_SSID     = 33;
static const unsigned NETWORK_PASSWORD = 65;
static const unsigned NETWORK_COUNTRY  = 3;

// Networks shown at once. A street of flats can hold more; the strongest are
// the ones anybody means.
static const unsigned NETWORK_SEEN = 24;

enum TNetworkKind
{
    NetworkOff,                         // `ether` not set
    NetworkCable,                       // `ether eth`
    NetworkWiFi,                        // `ether wlan`
    NetworkKindCount
};

// What the board is doing with the connection, as the caller reads it. The
// page turns it into a sentence; it never infers one itself.
enum TNetworkState
{
    NetStateIdle,                       // nothing started, nothing to say
    NetStateStarting,                   // the Wi-Fi is loading its firmware
    NetStateUnavailable,                // the chosen kind does not exist here
    NetStateSearching,
    NetStateReady,                      // running, not joined, nothing tried
    NetStateJoining,
    NetStateConnected,
    NetStateRefused,                    // the access point turned the key down
    NetStateLost,                       // was joined, is not any more
    NetStateCableUp,
    NetStateCableDown
};

struct TNetworkSeen
{
    char SSID[NETWORK_SSID];
    int  nDbm;                          // signal, as the chip reports it
    bool bSecured;                      // it asks for a password
};

struct TNetworkValues
{
    unsigned nKind;                     // TNetworkKind
    char     SSID[NETWORK_SSID];
    char     Password[NETWORK_PASSWORD];
    char     Country[NETWORK_COUNTRY];  // two capitals
};

struct TNetwork
{
    TNetworkValues V;
    TNetworkValues Opened;              // as the card had them
    // One bit per TNetworkKind this board can have at all. Zero means all of
    // them, so a caller that has not thought about it offers everything.
    unsigned nAvailable;
    // The kind running on this board right now, NetworkOff when none. Circle
    // cannot give a network device back, so changing it means restarting.
    unsigned nRunning;

    TNetworkSeen  Seen[NETWORK_SEEN];   // strongest first, one line per name
    unsigned      nSeen;
    TNetworkState State;
    char          Subject[NETWORK_SSID];// the network the state speaks of
    int           nDbm;                 // its signal, when connected
    unsigned      nMbps;                // the cable's speed, when up; 0 unknown

    bool          bShowPassword;
    // Set by the page when Join was refused before anything was tried; the
    // status line says why, until something changes.
    bool          bBadPassword;
    bool          bNoName;
};

enum TNetworkAction
{
    NetworkNothing,                     // the model changed; repaint and carry on
    NetworkRelayout,                    // Start changes its words: lay out again
    NetworkKindChanged,                 // the caller may start the network now
    NetworkSearch,
    NetworkJoin                         // SSID, password and country are checked
};

void NetworkDraw (TSurface *pSurface, TNetwork *pNetwork);
void NetworkRepaint (TSurface *pSurface);
unsigned NetworkWidgets (TWidget **ppList);
TRect NetworkDialog (void);
bool NetworkOverflowed (void);

// Brings the components in step with the model: the list from Seen, the status
// line from State, what is greyed from the kind. Called after the caller has
// changed the model — a scan that came back, a link that went up.
void NetworkSync (TNetwork *pNetwork);

TNetworkAction NetworkOperate (TNetwork *pNetwork, int nIndex);

// Whether the connection chosen differs from the one this board is already
// running: Circle cannot hand a network device back, so that takes a restart.
bool NetworkNeedsRestart (const TNetwork *pNetwork);

// Whether the password can be handed to WPA: empty for an open network, 8 to
// 63 printable ASCII characters, or 64 hex digits — what NetWpaConfigBuild()
// accepts, checked here so the page can say so before anything is tried.
bool NetworkPasswordUsable (const char *pPassword);

// Sorts by signal, strongest first, keeps one line per name (its strongest),
// drops hidden networks — they have no name to show — and keeps at most
// NETWORK_SEEN. In place; answers how many are left.
unsigned NetworkSortSeen (TNetworkSeen *pSeen, unsigned nCount);

// Four bars from a signal in dBm: -55 and stronger is four, -65 three, -75 two,
// anything weaker one.
unsigned NetworkBars (int nDbm);

#endif
