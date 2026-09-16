/*
 * mac_layout.h — where SheepShaver's Macintosh lives in memory.
 *
 * This is the arithmetic docs/topics/engine-powerpc.md explains, kept apart
 * from any allocation so it can be checked on a development machine. Getting it
 * wrong does not produce an error: it produces a Macintosh that reads someone
 * else's bytes, which is the hardest kind of fault to trace back.
 *
 * The model is direct addressing: a guest address becomes a host address by
 * adding one offset, so every region the Mac uses has to sit in one contiguous
 * host block, at the same distances from each other as the guest expects.
 *
 * Two of the Mac's regions are deliberately *not* in that block — Low Memory at
 * guest 0 and the Kernel Data at 0x68ffe000. Their addresses are fixed by the
 * ROM and are 1.6 GB apart, so covering them with one offset would mean
 * reserving 1.6 GB of host memory to use 20 KB of it. Upstream already banks
 * them into two static arrays for the hosts that cannot place them
 * (kpx_cpu/src/cpu/vm.hpp:207-218) and Okapia takes the same road.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_MAC_LAYOUT_H
#define OKAPIA_MAC_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

// Upstream's, checked against cpu_emulation.h where that header is available.
// Repeated rather than included because this file has to build on a machine
// that has no macemu tree configured, which is what makes it testable.
enum
{
    // 4 MB of ROM, in an area of 5: PatchROM() copies the last megabyte to the
    // 4 MB boundary for the 68k emulator (rom_patches.cpp:728).
    OKAPIA_ROM_SIZE      = 0x400000,
    OKAPIA_ROM_AREA_SIZE = 0x500000,
    // The PowerPC interrupt routine's stack, which upstream places just past
    // the ROM area (main_unix.cpp:2322).
    OKAPIA_SIG_STACK_SIZE = 0x10000,
    // SheepShaver's own 32-bit addressable scratch (thunks.h:122).
    OKAPIA_SHEEP_SIZE     = 0x80000,
    // The Macintosh's frame buffer. It has to be at a Mac address, because
    // screen_base is one and QuickDraw writes pixels through it — so it is part
    // of the block and not something allocated beside it.
    //
    // Sixteen megabytes, the same cap as the other engine's heap buffer
    // (video_circle.cpp, MAX_BUFFER): 2560x1440 in millions of colours, so both
    // Macintosh are offered the same modes on the same display. Taken once at
    // that size and never resized: a mode change must not allocate, and the
    // Macintosh changes depth whenever a dialog wants more colours.
    OKAPIA_FRAME_SIZE     = 0x1000000
};

// Fixed by the ROM, not by us, and the reason the block has a ceiling.
static const uint32_t OKAPIA_KERNEL_DATA_BASE = 0x68ffe000;

// The floor upstream states in a FIXME and never explains further
// (main_unix.cpp:182).
static const uint32_t OKAPIA_RAM_BASE_MIN = 0x04000000;

struct TMacLayout
{
    uint32_t nRAMBase;      // guest address of the first byte of Mac RAM
    uint32_t nRAMSize;
    uint32_t nROMBase;      // guest, aligned; the ROM area starts here
    uint32_t nSigStack;     // guest, just past the ROM area
    uint32_t nSheepBase;    // guest, just past that
    uint32_t nFrameBase;    // guest, the Mac's frame buffer
    uint32_t nEnd;          // guest, one past the last byte the block covers
    size_t   nHostBytes;    // what has to be allocated, in one piece
};

// Plan the block for a given amount of Mac RAM. False when it cannot be done,
// which is a refusal to start and not something to work around.
bool MacLayoutPlan (uint32_t nRAMSize, TMacLayout *pOut);

// The offset that turns a guest address into a host one, given where the block
// actually landed. Everything else follows from it.
static inline uintptr_t MacLayoutBaseDiff (const TMacLayout *pLayout,
                                           void *pHostBlock)
{
    return (uintptr_t) pHostBlock - (uintptr_t) pLayout->nRAMBase;
}

#endif
