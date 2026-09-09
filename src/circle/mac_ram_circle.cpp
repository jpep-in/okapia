//
// mac_ram_circle.cpp — see mac_ram_circle.h.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "okapia_circle.h"

#include <circle/memory.h>

#include "mac_ram_circle.h"

#define FROM "okapia-macram"

static void  *s_pBlock;
static size_t s_nBytes;

void *MacRamClaim (size_t nBytes)
{
    if (s_pBlock != 0)
    {
        if (nBytes <= s_nBytes)
        {
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "Reusing the %u MB block for the other Macintosh",
                                    (unsigned) (s_nBytes / (1024 * 1024)));
            return s_pBlock;
        }
        CLogger::Get ()->Write (FROM, LogError,
                                "The block is %u MB and %u MB are wanted: the first "
                                "claim must be the largest",
                                (unsigned) (s_nBytes / (1024 * 1024)),
                                (unsigned) (nBytes / (1024 * 1024)));
        return 0;
    }

    // HEAP_ANY: above 1 GB when the board has it, low memory otherwise. A raw
    // block and not operator new — there are no constructors to run over a
    // quarter of a gigabyte.
    s_pBlock = CMemorySystem::HeapAllocate (nBytes, HEAP_ANY);
    if (s_pBlock == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot allocate %u MB for the Mac",
                                (unsigned) (nBytes / (1024 * 1024)));
        return 0;
    }
    s_nBytes = nBytes;
    return s_pBlock;
}
