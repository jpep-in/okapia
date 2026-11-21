/*
 * net_bench_circle.h — what the board's network can carry, with no Macintosh
 * in the way.
 *
 * The Macintosh's own transfers say what the whole chain gives — the guest's
 * TCP, the emulated CPU, the ring, the interface — and nothing about which of
 * them is the limit. This measures the last two alone: UDP datagrams as fast as
 * the interface takes them, then as fast as it gives them, against
 * scripts/netbench.py on another machine, which counts the other end.
 *
 * Built only with OKAPIA_NETBENCH=1 (src/kernel/Makefile). Asked for with the
 * `netbench` preference: the other machine's address, and
 * this board's after it when nothing on the cable serves DHCP; it
 * runs once per power-on, before the window and before the Macintosh, and says
 * what it found in the log. Nothing here runs when the preference is empty.
 *
 * The protocol, all on UDP port 9000 of the other machine, from 9001 here:
 *   board  -> HELLO                       until the script answers
 *   script -> TX <seconds>                the board sends for that long,
 *   board  -> data… then DONE <frames> <bytes> <us>
 *   script -> RX <seconds>                the script sends for that long,
 *   script -> data… then END <frames>
 *   board  -> RESULT <frames> <bytes> <us> <first..last span>
 * Text messages start with "OKBN ", data with "OKBD" and a 32-bit sequence.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_NET_BENCH_CIRCLE_H
#define OKAPIA_NET_BENCH_CIRCLE_H

#include <circle/types.h>

// From the firmware's start: does nothing unless `netbench` names a machine,
// and nothing after the first time. In a kernel built without the bench it
// only says, once, that a `netbench` line is ignored.
void NetBenchRun (void);

// From BoardNetPoll(): true for a frame that was the bench's. Without the
// bench, nothing is ever the bench's, and the test costs nothing per frame.
#if OKAPIA_NETBENCH
bool NetBenchTake (const u8 *pFrame, unsigned nLength);
#else
static inline bool NetBenchTake (const u8 *, unsigned)
{
    return false;
}
#endif

#endif
