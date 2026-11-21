/*
 * execmem_circle.cpp — see execmem_circle.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "execmem_circle.h"

#include <circle/alloc.h>
#include <stdlib.h>
#include <circle/logger.h>
#include <circle/types.h>

static const char FROM[] = "execmem";

// Circle's layout (lib/translationtable64.cpp, include/circle/armv8mmu.h):
// 64 KB granule, TTBR0_EL1 points at the level 2 table, whose entries (one per
// 512 MB) are table descriptors pointing at a level 3 table of 8192 page
// descriptors. PXN is bit 53 of a level 3 page descriptor.
static const u64 PAGE = 0x10000;
static const u64 PXN = 1ULL << 53;
static const u64 TABLE_ADDR_MASK = 0x0000FFFFFFFF0000ULL;   // bits [47:16]

// Where the level 3 descriptor of `addr` lives, or 0 if the level 2 entry is
// not a table descriptor.
static volatile u64 *Level3Entry (u64 addr)
{
    u64 ttbr0;
    asm volatile ("mrs %0, ttbr0_el1" : "=r" (ttbr0));
    const volatile u64 *l2 = (const volatile u64 *) (ttbr0 & TABLE_ADDR_MASK);

    const u64 d2 = l2[addr >> 29];
    if ((d2 & 3) != 3)
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "level 2 entry for %lx is %lx: not a table", addr, d2);
        return 0;
    }
    volatile u64 *l3 = (volatile u64 *) (d2 & TABLE_ADDR_MASK);
    return &l3[(addr >> 16) & 8191];
}

bool ExecMemMakeExecutable (void *pStart, size_t nLength)
{
    const u64 start = (u64) pStart;
    if ((start & (PAGE - 1)) != 0 || (nLength & (PAGE - 1)) != 0 || nLength == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "range %lx + %lx is not on 64 KB pages",
                                start, (u64) nLength);
        return false;
    }

    for (u64 addr = start; addr < start + nLength; addr += PAGE)
    {
        volatile u64 *desc = Level3Entry (addr);
        if (desc == 0)
        {
            return false;
        }
        *desc = *desc & ~PXN;
    }

    asm volatile ("dsb ish" ::: "memory");
    for (u64 addr = start; addr < start + nLength; addr += PAGE)
    {
        asm volatile ("tlbi vaae1is, %0" : : "r" (addr >> 12) : "memory");
    }
    asm volatile ("dsb ish\n\tisb" ::: "memory");
    return true;
}

#ifdef EXECMEM_SELFTEST

static void Emit (u32 *code, u32 value)
{
    code[0] = 0x52800000u | ((value & 0xFFFF) << 5);   // mov w0, #value
    code[1] = 0xD65F03C0u;                              // ret
    __builtin___clear_cache ((char *) code, (char *) (code + 2));
}

static int Call (u32 *code)
{
    int (*volatile fn) (void) = (int (*) (void)) code;
    return fn ();
}

void ExecMemSelfTest (void)
{
    // A 64 KB page of its own, 64 KB aligned: nothing else shares its
    // descriptor. Circle's memalign refuses more than its heap block alignment,
    // so take three pages and align inside: the aligned page and the one after
    // it both lie within the block. Never freed, as a JIT arena would not be.
    u8 *block = (u8 *) malloc (3 * PAGE);
    if (block == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "FAIL: no memory for the region");
        return;
    }
    u8 *region = (u8 *) (((u64) block + PAGE - 1) & ~(PAGE - 1));
    const u64 addr = (u64) region;

    volatile u64 *desc = Level3Entry (addr);
    // The page after it: untouched, and it must still be non-executable.
    volatile u64 *next = Level3Entry (addr + PAGE);
    if (desc == 0 || next == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "FAIL: no level 3 descriptor");
        return;
    }

    const u64 before = *desc;
    CLogger::Get ()->Write (FROM, LogNotice, "region %lx, level 3 descriptor %lx = %lx",
                            addr, (u64) desc, before);

    if (!ExecMemMakeExecutable (region, PAGE))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "FAIL: the page could not be changed");
        return;
    }
    const u64 after = *desc;
    CLogger::Get ()->Write (FROM, LogNotice, "descriptor after clearing PXN: %lx", after);

    u32 *code = (u32 *) region;
    Emit (code, 0x1234);
    const int first = Call (code);
    Emit (code, 0x5678);
    const int second = Call (code);

    const u64 nextDesc = *next;
    const bool ok =    (before & PXN) != 0 && (after & PXN) == 0
                    && first == 0x1234 && second == 0x5678
                    && (nextDesc & PXN) != 0;
    CLogger::Get ()->Write (FROM, ok ? LogNotice : LogWarning,
                            "%s: first call %x (want 1234), re-emitted call %x (want 5678), "
                            "PXN before %d after %d, next page descriptor %lx PXN %d",
                            ok ? "PASS" : "FAIL", first, second,
                            (int) ((before & PXN) != 0), (int) ((after & PXN) != 0),
                            nextDesc, (int) ((nextDesc & PXN) != 0));
    // The page stays executable and allocated: a JIT arena would keep it.
}

#else

void ExecMemSelfTest (void)
{
}

#endif
