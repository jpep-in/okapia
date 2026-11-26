/*
 * clock_okapia.h — the board's clock: where its time comes from, and how the
 * Macintosh hears of it.
 *
 * One clock for the whole board, Circle's, kept in local time because that is
 * what a Macintosh of this era keeps (docs/topics/clock-and-pram.md). Four
 * sources raise or set it, in this order of trust:
 *
 *   1. the network (NTP), once the interface is up — the truth, and final;
 *   2. the clock set on the Macintosh, kept on the card (TClockRecord);
 *   3. the latest modification date of the volumes on the card;
 *   4. the build time.
 *
 * Shared by both engines and the firmware: compiled once (SHARED_SRCS), since
 * two copies would be two clocks. What touches the Macintosh's memory is the
 * engine's (tick_okapia.cpp); this only says what to write there.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CLOCK_CIRCLE_H
#define OKAPIA_CLOCK_CIRCLE_H

#include <circle/types.h>

#include "okapia_clock.h"

enum TClockSource
{
    ClockFromBuild,
    ClockFromVolume,
    ClockFromMac,
    ClockFromNetwork
};

// At startup, once: the zone from `zone` (or the fixed `timezone`), then the
// build time. Logs the result.
void ClockInitialize (void);

// The boot menu changed them. The zone moves the clock at once; a new server
// is asked at once if the network is up.
void ClockSetZone (const char *pKey, int nFixedMinutes);
void ClockSetServer (const char *pServer);

// From each engine's periodic seam and from the boot menu's loop, on the
// emulation thread: summer time, the clock the Macintosh set, the card.
void ClockPoll (void);

// The Macintosh's Time global (0x20C), once a second, from the tick interrupt.
// nRead is what it holds now; answers true with what to write there. A value
// the Macintosh wrote itself is taken as the clock set from Date & Time, and
// ClockPoll() makes it the board's.
void ClockMacStart (void);
bool ClockMacSecond (u32 nRead, u32 *pWrite);

// For CardRefineClock() (card_okapia.cpp, per engine).
bool ClockSynced (void);                    // the network answered this power-on
bool ClockDecided (void);                   // a source already spoke this power-on
bool ClockRecordRead (TClockRecord *pRecord);
void ClockRecordForget (const char *pWhy);
void ClockNoteSource (TClockSource Source, const char *pDetail);

// Where the time came from, for the information page. *ppDetail is the server
// or the volume, or empty.
TClockSource ClockSource (const char **ppDetail);

// Before the board stops or restarts: the clock set on the Macintosh, brought
// up to now, so the next start does not go back to the moment it was set.
void ClockSessionEnd (void);

// From board_net_circle.cpp. Take answers true for a frame that was the
// clock's question and must not reach the Macintosh; Step runs the timers and
// starts a question once the link is up.
bool ClockNetTake (const u8 *pFrame, unsigned nLength);
void ClockNetStep (bool bLinkUp);

// The address the clock's DHCP lease gave this board, kept since: the network
// bench (net_bench_circle.cpp) speaks from it. False before any lease.
bool ClockNetLease (u32 *pAddress);

#endif
