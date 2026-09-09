//
// mac_ram_circle.h — the one big block, shared by both emulators.
//
// A quarter of a gigabyte is not something an image that carries two engines
// can allocate twice: Circle serves blocks larger than its largest bucket by
// walking forward through free space, so the second request either fails on a
// 1 GB board or succeeds and leaves the board with nothing (AGENTS.md, resource
// budget). Only one Macintosh runs at a time, so only one block is needed — and
// the engine that takes over inherits it rather than asking for its own.
//
// Claimed at the largest size any engine asks for, and never given back. The
// two engines lay their regions out differently inside it, which is their
// business: this only owns the extent.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_mac_ram_circle_h
#define _okapia_mac_ram_circle_h

#include <stddef.h>

// What every engine adds on top of the Mac's RAM, so that the first claim is
// big enough for the second engine too and the block is never re-taken.
// SheepShaver needs the most: 5 MB of ROM area, a 64 KB interrupt stack, 512 KB
// of SheepMem and four megabytes of frame buffer — 1024x768 in millions of
// colours — plus up to a megabyte of alignment
// (src/circle/sheepshaver/mac_layout.h). Sixteen covers it with room that costs
// nothing next to 256.
#define OKAPIA_MAC_BLOCK_OVERHEAD (16 * 1024 * 1024)

// The block, or 0. A second call with the same or a smaller size answers the
// same pointer and allocates nothing; a larger one is refused rather than
// quietly served from a second block, because that is the bug this exists to
// prevent and it should be read about, not survived.
extern void *MacRamClaim (size_t nBytes);

#endif
