/*
 * trace_fpu_circle.cpp — how much the Macintosh actually uses its FPU, and what it costs.
 *
 * The question this answers is the one that decides whether the floating-point
 * core is worth an argument. A conversion test proves the arithmetic is right;
 * it says nothing about whether any Macintosh ever runs it. Counting the
 * instructions does — and it is the difference between "the round trip is
 * lossless" and "the round trip is lossless and the guest takes it".
 *
 * Since the translator, the second question is where the time goes: in the core
 * (the arithmetic and its conversions) or around it (the trip out of translated
 * code). Each call is timed with the generic timer, which needs no set-up and
 * traps at no exception level; at 54 MHz on a Pi 4 one tick is 28 CPU cycles,
 * too coarse for one call but unbiased over thousands. Every ten seconds in which
 * the guest used its FPU, the log gets the share of wall time spent inside the
 * core and the operations that took most of it — run one Speedometer FPU test at
 * a time to read them apart.
 *
 * Hooked at link time rather than by patching external/ (docs/contributing/testing-and-debugging.md), so the
 * spelling is __wrap_ plus the mangled name and nothing more.
 *
 * Off unless src/kernel/Makefile passes OKAPIA_TRACE=1.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "sysdeps.h"
#include "okapia_circle.h"

#define FROM "okapia-fpu"

// Slots 0-127 are the general operations by their extension word's low seven
// bits (FADD 0x22, FMUL 0x23, FSIN 0x0e, ...), whatever the source; the four
// after them are the forms that are not arithmetic.
enum
{
    SlotMovecr  = 128,      // FMOVECR, the constant ROM
    SlotMoveOut = 129,      // FMOVE FPn,<ea>
    SlotControl = 130,      // FMOVE(M) to or from FPCR, FPSR, FPIAR
    SlotMovem   = 131,      // FMOVEM of data registers
    SlotCount   = 132
};

struct TSlot
{
    unsigned long nCount;
    unsigned long nTicks;
};

// Static, written in place: nothing here may allocate (AGENTS.md, resource budget).
static TSlot s_Total[SlotCount];
static TSlot s_Window[SlotCount];
static unsigned long s_nSource[8];          // <ea> source formats: L S X P W D B, 7 unused
static unsigned long s_nBranches;           // FBcc, FDBcc, FScc: the conditionals
static unsigned long s_nArithmetic;
static unsigned long s_nWindowStart;        // generic timer, start of the current window
static unsigned long s_nOverhead;           // ticks a timed empty call reads, subtracted

static inline unsigned long Now (void)
{
    unsigned long n;
    asm volatile ("isb; mrs %0, cntvct_el0" : "=r" (n) :: "memory");
    return n;
}

static inline unsigned long Frequency (void)
{
    unsigned long n;
    asm volatile ("mrs %0, cntfrq_el0" : "=r" (n));
    return n;
}

static const char *Name (unsigned nSlot)
{
    static const char *const s_Names[0x40] =
    {
        "FMOVE",   "FINT",    "FSINH",   "FINTRZ",  "FSQRT",   "?05",     "FLOGNP1", "?07",
        "FETOXM1", "FTANH",   "FATAN",   "?0b",     "FASIN",   "FATANH",  "FSIN",    "FTAN",
        "FETOX",   "FTWOTOX", "FTENTOX", "?13",     "FLOGN",   "FLOG10",  "FLOG2",   "?17",
        "FABS",    "FCOSH",   "FNEG",    "?1b",     "FACOS",   "FCOS",    "FGETEXP", "FGETMAN",
        "FDIV",    "FMOD",    "FADD",    "FMUL",    "FSGLDIV", "FREM",    "FSCALE",  "FSGLMUL",
        "FSUB",    "?29",     "?2a",     "?2b",     "?2c",     "?2d",     "?2e",     "?2f",
        "FSINCOS", "FSINCOS", "FSINCOS", "FSINCOS", "FSINCOS", "FSINCOS", "FSINCOS", "FSINCOS",
        "FCMP",    "?39",     "FTST",    "?3b",     "?3c",     "?3d",     "?3e",     "?3f",
    };

    switch (nSlot)
    {
    case SlotMovecr:  return "FMOVECR";
    case SlotMoveOut: return "FMOVE out";
    case SlotControl: return "FMOVE ctl";
    case SlotMovem:   return "FMOVEM";
    }
    if (nSlot < 0x40)
    {
        return s_Names[nSlot];
    }
    // 0x40-0x7f: the 68040's single- and double-rounding forms (FSADD, FDMUL...),
    // named by their base operation.
    return s_Names[nSlot & 0x3f];
}

static unsigned Slot (uint32 nExtra)
{
    switch ((nExtra >> 13) & 7)
    {
    case 0:                                         // FPm -> FPn
        return nExtra & 0x7f;
    case 2:                                         // <ea> -> FPn, or the constant ROM
        if ((nExtra & 0xfc00) == 0x5c00)
        {
            return SlotMovecr;
        }
        s_nSource[(nExtra >> 10) & 7]++;
        return nExtra & 0x7f;
    case 3:
        return SlotMoveOut;
    case 4:
    case 5:
        return SlotControl;
    default:
        return SlotMovem;
    }
}

static void Report (unsigned long nNow)
{
    const unsigned long nFrequency = Frequency ();
    const unsigned long nWall      = nNow - s_nWindowStart;

    unsigned long nCount = 0, nTicks = 0;
    for (unsigned i = 0; i < SlotCount; i++)
    {
        nCount += s_Window[i].nCount;
        nTicks += s_Window[i].nTicks;
    }

    // Per mille of the window's wall time spent inside the core, and the mean
    // nanoseconds a call took there.
    CLogger::Get ()->Write (FROM, LogNotice,
                            "%lu FPU instructions in %lu ms, %lu.%lu %% of the time inside the core, %lu ns each",
                            nCount, nWall * 1000 / nFrequency,
                            nTicks * 1000 / nWall / 10, nTicks * 1000 / nWall % 10,
                            nTicks * 1000000000UL / nFrequency / nCount);

    // The six slots that took most of the window, by time.
    bool bShown[SlotCount] = {};
    for (unsigned nRank = 0; nRank < 6; nRank++)
    {
        unsigned nBest = SlotCount;
        for (unsigned i = 0; i < SlotCount; i++)
        {
            if (!bShown[i] && s_Window[i].nCount != 0
                && (nBest == SlotCount || s_Window[i].nTicks > s_Window[nBest].nTicks))
            {
                nBest = i;
            }
        }
        if (nBest == SlotCount)
        {
            break;
        }
        bShown[nBest] = true;

        const TSlot &Slot = s_Window[nBest];
        CLogger::Get ()->Write (FROM, LogNotice, "  %-9s (%02x) %9lu calls %6lu ns each, %3lu %% of core time",
                                Name (nBest), nBest < 0x80 ? nBest : 0, Slot.nCount,
                                Slot.nTicks * 1000000000UL / nFrequency / Slot.nCount,
                                nTicks != 0 ? Slot.nTicks * 100 / nTicks : 0);
    }

    CLogger::Get ()->Write (FROM, LogNotice,
                            "  sources L %lu S %lu X %lu P %lu W %lu D %lu B %lu; conditionals %lu (totals)",
                            s_nSource[0], s_nSource[1], s_nSource[2], s_nSource[3],
                            s_nSource[4], s_nSource[5], s_nSource[6], s_nBranches);

    for (unsigned i = 0; i < SlotCount; i++)
    {
        s_Total[i].nCount += s_Window[i].nCount;
        s_Total[i].nTicks += s_Window[i].nTicks;
        s_Window[i].nCount = 0;
        s_Window[i].nTicks = 0;
    }
}

static void Calibrate (void)
{
    // The cost of two timer reads around nothing, so that it is not charged to
    // the operations. Taken as the minimum of a few, which is what it costs
    // when nothing interrupts it.
    unsigned long nMin = ~0UL;
    for (unsigned i = 0; i < 64; i++)
    {
        const unsigned long nStart = Now ();
        const unsigned long nTicks = Now () - nStart;
        if (nTicks < nMin)
        {
            nMin = nTicks;
        }
    }
    s_nOverhead = nMin;
}

// fpuop_arithmetic(uae_u32 opcode, uae_u32 extra), which is every FMOVE, FADD,
// FSIN and the rest — the whole of the 68881's work except the branch and the
// state frames.
extern "C" void __real__Z16fpuop_arithmeticjj (uint32 opcode, uint32 extra);

extern "C" void __wrap__Z16fpuop_arithmeticjj (uint32 opcode, uint32 extra)
{
    s_nArithmetic++;

    // The first few say the path is alive at all.
    if (s_nArithmetic <= 3)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "FPU instruction %lu: %04x %04x",
                                s_nArithmetic, opcode & 0xffff, extra & 0xffff);
        if (s_nArithmetic == 1)
        {
            Calibrate ();
            s_nWindowStart = Now ();
        }
    }

    const unsigned nSlot  = Slot (extra);
    const unsigned long nStart = Now ();
    __real__Z16fpuop_arithmeticjj (opcode, extra);
    const unsigned long nEnd = Now ();

    const unsigned long nTicks = nEnd - nStart;
    s_Window[nSlot].nCount++;
    s_Window[nSlot].nTicks += nTicks > s_nOverhead ? nTicks - s_nOverhead : 0;

    // A window closes after ten seconds; it is reported only if the guest used
    // its FPU in it, which every call here means it did.
    if (nEnd - s_nWindowStart >= 10 * Frequency ())
    {
        Report (nEnd);
        s_nWindowStart = Now ();
    }
}

// The conditionals are counted, not timed: their cost is the trip, not the core.
extern "C" void __real__Z9fpuop_bccjjj (uint32 opcode, uint32 pc, uint32 extra);
extern "C" void __wrap__Z9fpuop_bccjjj (uint32 opcode, uint32 pc, uint32 extra)
{
    s_nBranches++;
    __real__Z9fpuop_bccjjj (opcode, pc, extra);
}

extern "C" void __real__Z10fpuop_dbccjj (uint32 opcode, uint32 extra);
extern "C" void __wrap__Z10fpuop_dbccjj (uint32 opcode, uint32 extra)
{
    s_nBranches++;
    __real__Z10fpuop_dbccjj (opcode, extra);
}

extern "C" void __real__Z9fpuop_sccjj (uint32 opcode, uint32 extra);
extern "C" void __wrap__Z9fpuop_sccjj (uint32 opcode, uint32 extra)
{
    s_nBranches++;
    __real__Z9fpuop_sccjj (opcode, extra);
}
