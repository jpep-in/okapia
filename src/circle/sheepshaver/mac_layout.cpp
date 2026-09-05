/*
 * mac_layout.cpp — the arithmetic, and the three refusals.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "mac_layout.h"

// The ROM has to start on a megabyte boundary. Upstream aligns it that way on
// the one platform where it chooses the address itself (main_unix.cpp:184,
// ROM_ALIGNMENT), and there is no reason to be less tidy where we choose it.
static const uint32_t ROM_ALIGNMENT = 0x100000;

bool MacLayoutPlan (uint32_t nRAMSize, TMacLayout *pOut)
{
    // Upstream keeps RAM at 0x10000000 and the ROM 1 GB above it, because on a
    // Unix host both addresses have to be asked for and granted separately.
    // Here the block is ours to place, so the ROM follows the RAM immediately
    // and the gap — 1 GB of host address space that would have to be covered
    // for nothing — disappears. That is the whole of §19.4 in one line.
    pOut->nRAMBase = 0x10000000;
    pOut->nRAMSize = nRAMSize;

    if (nRAMSize == 0)
    {
        return false;
    }

    // Every step below can carry past 4 GB, and a guest address is 32 bits.
    // Done in 64 bits and checked at each stage rather than trusted: a wrap
    // here would place the ROM under the RAM and nothing would say so.
    uint64_t nCursor = (uint64_t) pOut->nRAMBase + nRAMSize;

    nCursor = (nCursor + ROM_ALIGNMENT - 1) & ~(uint64_t) (ROM_ALIGNMENT - 1);
    pOut->nROMBase = (uint32_t) nCursor;
    nCursor += OKAPIA_ROM_AREA_SIZE;

    pOut->nSigStack = (uint32_t) nCursor;
    nCursor += OKAPIA_SIG_STACK_SIZE;

    pOut->nSheepBase = (uint32_t) nCursor;
    nCursor += OKAPIA_SHEEP_SIZE;

    pOut->nEnd = (uint32_t) nCursor;

    // 1. Below the floor upstream states, the ROM's own code misbehaves in ways
    //    nobody has written down. It is not a limit worth testing.
    if (pOut->nRAMBase < OKAPIA_RAM_BASE_MIN)
    {
        return false;
    }

    // 2. The Kernel Data sits at an address the ROM fixes, and upstream refuses
    //    outright when RAM reaches it (main_unix.cpp:1110). The block must end
    //    below it, not merely start below it.
    if (nCursor > OKAPIA_KERNEL_DATA_BASE)
    {
        return false;
    }

    // 3. And the arithmetic must not have wrapped, which the checks above would
    //    otherwise let through: a wrapped end is a small number.
    if (pOut->nROMBase <= pOut->nRAMBase || pOut->nEnd <= pOut->nROMBase)
    {
        return false;
    }

    pOut->nHostBytes = (size_t) (pOut->nEnd - pOut->nRAMBase);
    return true;
}
