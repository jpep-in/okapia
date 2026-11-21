/*
 * board_net_okapia.h — the board's network interface, claimed once.
 *
 * The Macintosh shares the Pi's MAC address (docs/topics/network.md): its
 * frames go out through a Circle CNetDevice as they are, and every frame the
 * interface receives is the Macintosh's — Okapia runs no network stack of its
 * own. Two exceptions: on Wi-Fi the WPA handshake (EAPOL, ethertype 0x888E),
 * which belongs to the supplicant, and the answers to the clock's question to
 * the network (clock_okapia.h), which carry a transaction number or a port of
 * their own. Neither reaches the Mac.
 *
 * The device is the board's and not an engine's, for the reason the sound
 * device is: the Macintosh goes round more than once and there are two of
 * them, and Circle gives nothing back — a second CBcm4343Device or
 * CScheduler asserts. So the first start claims it, every later start is
 * handed the same one, and an engine stopping only stops listening.
 *
 * Frames received are copied into a ring allocated at the first start. The
 * engine drains it from its own interrupt (ether_okapia.cpp); when nobody
 * listens — between two starts, during the boot menu — they are dropped, which
 * is what a Macintosh with its Ethernet driver closed does too.
 *
 * Everything here runs on the emulation thread, from an engine's periodic seam
 * or from the boot menu's loop, never from an interrupt: the Wi-Fi driver and
 * its supplicant are Circle tasks, and they only run when this thread yields.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_BOARD_NET_CIRCLE_H
#define OKAPIA_BOARD_NET_CIRCLE_H

#include <circle/types.h>

// What the `ether` preference asks for. Anything else is no network.
enum TBoardNetKind
{
    BoardNetNone,
    BoardNetWired,      // `ether eth`: the Pi 4's own port, or a USB adapter
    BoardNetWLAN        // `ether wlan`: the on-board Wi-Fi, SSID from the prefs
};

// Reads `nonet`, `ether` and, for Wi-Fi, `wifissid`, `wifipassword` and
// `wificountry`, and brings the interface up. Only the first call does
// anything; later ones answer what it answered. False means the Macintosh has
// no network this session, and the log says why. Blocks while the Wi-Fi
// firmware loads, about a second.
bool BoardNetStart (void);

// The kind that was started, BoardNetNone when none was.
TBoardNetKind BoardNetKind (void);

// Whether a device was asked for already — successfully or not. A start that
// found no `ether` to obey does not count: the boot menu may set one later.
bool BoardNetTried (void);

// Whether the link is up, and at what speed in Mbit/s when the device says (0
// otherwise). For the boot menu's status line; false when nothing started.
bool BoardNetLinkUp (unsigned *pMbps);

// The address the Macintosh uses: the interface's own.
const u8 *BoardNetMAC (void);

// The engine that runs the Macintosh starts and stops listening. Stopping
// empties the ring and forgets the multicast groups, as a driver closing does.
void BoardNetListen (bool bListen);

// From the engine's periodic seam, about a thousand times a second, and from
// the boot menu's loop: lets the Wi-Fi tasks run, moves received frames into
// the ring, and watches the link. Cheap when there is nothing to do.
void BoardNetPoll (void);

// Whether a frame is waiting in the ring.
bool BoardNetPending (void);

// Empties the ring, for a Macintosh whose driver is not open yet: nothing in
// it may run before the System has a heap to put a frame in.
void BoardNetDiscard (void);

// Takes the oldest frame out of the ring. pBuffer holds at least 1514 bytes;
// answers the frame's length, 0 when the ring is empty.
unsigned BoardNetReceive (u8 *pBuffer);

// Puts a frame in the ring as if the interface had received it. For the
// trace build's self-test (ether_okapia.cpp, NET_SELFTEST), which proves the
// path from the ring to the Macintosh and back where QEMU's USB network
// cannot deliver a frame (docs/topics/network.md).
bool BoardNetInject (const u8 *pFrame, unsigned nLength);

// Sends one Ethernet frame, source address already set. Short frames are
// padded to the Ethernet minimum here.
bool BoardNetSend (const u8 *pFrame, unsigned nLength);

// Multicast groups the Macintosh joins and leaves (AppleTalk, mDNS, ...).
// False when the list is full; the interface is told whenever it changes.
bool BoardNetJoinGroup (const u8 *pAddress);
bool BoardNetLeaveGroup (const u8 *pAddress);

#endif
