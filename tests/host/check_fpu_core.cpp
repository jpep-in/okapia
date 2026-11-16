/*
 * check_fpu_core.cpp — the 68881 as the Macintosh sees it: instructions, not functions.
 *
 * check_fpu tests the softfloat; this tests fpu_softfloat.cpp around it, the
 * part that decodes an instruction, fetches its operand, rounds as FPCR says,
 * sets the condition codes and the exception bytes, and stores the result.
 * It runs a fixed pseudo-random stream of FPU instructions through
 * fpuop_arithmetic() — every operation, register and memory operands in every
 * format, FMOVECR, packed decimal both ways — over operands that include
 * zeros, infinities, NaNs of both kinds, denormals and unnormals, under every
 * FPCR mode and precision, and folds the whole FPU state and the memory
 * operand into one hash after each instruction.
 *
 * The hash is pinned: a change to the core that is meant to change no result
 * (a speed-up, a cleanup) must leave it as it is. And no instruction may take
 * more than a few seconds: the first run of this stream found FSIN of an
 * unnormal near 2^3919 never returning, because the core passed unnormals to
 * the FPSP's algorithms; it now normalises them first, as the hardware does.
 *
 * Only what the core touches is supplied here: the 68k registers, guest
 * memory at a host address, and the two exits it may take.
 *
 *   ./check_fpu_core          the stream and the regression
 *   ./check_fpu_core N        print the hash after N instructions
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"
#define FPU_IMPLEMENTATION
#include "fpu/fpu.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// What fpu_softfloat.cpp needs from the rest of the core.
struct regstruct regs;
uintptr MEMBaseDiff;
int CPUType = 4;
uae_u32 get_disp_ea_020 (uae_u32 base, uae_u32) { return base; }
static unsigned s_nIllegal;
void op_illg (uae_u32) { s_nIllegal++; }
void Exception (int, uaecptr) {}

static uae_u8 s_Memory[65536];
static const uaecptr Operand = 0x1000;

static volatile long s_nAt;
static volatile uae_u32 s_nOpcode, s_nExtra;

static void Stuck (int)
{
    fprintf (stderr, "  FAIL instruction %ld (%04x %04x) did not return\n",
             s_nAt, (unsigned) s_nOpcode, (unsigned) s_nExtra);
    _exit (1);
}

// One instruction, under a watchdog.
static void Run (uae_u32 nOpcode, uae_u32 nExtra)
{
    s_nOpcode = nOpcode;
    s_nExtra = nExtra;
    alarm (5);
    fpuop_arithmetic (nOpcode, nExtra);
    alarm (0);
}

static uint64_t s_nState = 0x243f6a8885a308d3ULL;

static uint64_t Next (void)
{
    s_nState += 0x9e3779b97f4a7c15ULL;
    uint64_t z = s_nState;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void SetRegister (int r, uae_u16 nHigh, uae_u64 nLow)
{
    fpu.registers[r].high = nHigh;
    fpu.registers[r].low = nLow;
}

static void RandomRegister (int r)
{
    const uint64_t x = Next ();
    const uae_u16 nSign = (uae_u16) ((x >> 63) << 15);
    const uint64_t m = Next () | (1ULL << 63);
    switch (x & 15)
    {
    case 0:  SetRegister (r, nSign, 0);                                     break;  // zero
    case 1:  SetRegister (r, nSign | 0x7fff, 0);                            break;  // infinity
    case 2:  SetRegister (r, nSign | 0x7fff, m);                            break;  // quiet NaN
    case 3:  SetRegister (r, nSign | 0x7fff, (m & ~(1ULL << 62)) | 1);      break;  // signalling NaN
    case 4:  SetRegister (r, nSign, m >> 3);                                break;  // denormal
    case 5:  SetRegister (r, nSign | 0x3ff0, m >> 2);                       break;  // unnormal
    case 6:  SetRegister (r, nSign | (uae_u16) (1 + (x >> 8) % 0x7ffe), m); break;  // any exponent
    default: SetRegister (r, nSign | (uae_u16) (0x3fff + (int) ((x >> 8) & 0x3f) - 32), m); break;
    }
}

static uint64_t s_nHash = 0xcbf29ce484222325ULL;

static void Fold (uint64_t v)
{
    for (int i = 0; i < 8; i++)
    {
        s_nHash ^= (v >> (i * 8)) & 0xff;
        s_nHash *= 0x100000001b3ULL;
    }
}

static uint64_t Stream (long nCount)
{
    static const unsigned short Ops[] =
    {
        0x00, 0x01, 0x03, 0x04, 0x18, 0x1a, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
        0x25, 0x26, 0x27, 0x28, 0x38, 0x3a, 0x0e, 0x1d, 0x10, 0x14, 0x30,
        0x40, 0x44, 0x41, 0x45, 0x58, 0x5c, 0x5a, 0x5e, 0x60, 0x64, 0x62, 0x66,
        0x63, 0x67, 0x68, 0x6c
    };
    static const unsigned Sizes[] = { 0, 1, 2, 4, 5, 6 };   // L S X W D B

    for (long i = 0; i < nCount; i++)
    {
        const uint64_t x = Next ();
        if ((i & 63) == 0)
        {
            // FPCR: every mode and precision, INEX2 enabled now and then.
            fpu_set_fpcr ((uae_u32) (x & 0xf0) | ((x >> 8) & 1 ? 0x200 : 0));
            if ((x >> 9) & 1)
            {
                fpu_set_fpsr (0);
            }
        }
        if ((i & 7) == 0)
        {
            RandomRegister ((int) ((x >> 12) & 7));
            RandomRegister ((int) ((x >> 15) & 7));
        }
        // The memory operand at A0: random bytes, or a double near 1.
        m68k_areg (regs, 0) = Operand;
        for (int k = 0; k < 12; k++)
        {
            s_Memory[Operand + k] = (uae_u8) (Next () >> 56);
        }
        if ((x >> 18) & 1)
        {
            s_Memory[Operand] = 0x3f;
            s_Memory[Operand + 1] = (uae_u8) (0xf0 | ((x >> 19) & 0xf));
        }

        const unsigned nSource = (x >> 23) & 7, nDest = (x >> 26) & 7;
        const unsigned nOp = Ops[(x >> 29) % (sizeof Ops / sizeof Ops[0])];
        uae_u32 nOpcode = 0xf200, nExtra;
        switch ((x >> 40) & 7)
        {
        case 0: case 1: case 2: case 3:     // FPm,FPn
            nExtra = (nSource << 10) | (nDest << 7) | nOp;
            break;
        case 4:                             // (A0),FPn in every format but packed
            nOpcode = 0xf210;
            nExtra = 0x4000 | (Sizes[(x >> 43) % 6] << 10) | (nDest << 7) | nOp;
            break;
        case 5:                             // FMOVE FPn,(A0) in every format but packed
            nOpcode = 0xf210;
            nExtra = 0x6000 | (Sizes[(x >> 43) % 6] << 10) | (nSource << 7);
            break;
        case 6:                             // FMOVE.P, out with a static k-factor or in
            nOpcode = 0xf210;
            nExtra = (x >> 46) & 1 ? (0x6000 | (3 << 10) | (nSource << 7) | ((x >> 47) & 0x7f))
                                   : (0x4000 | (3 << 10) | (nDest << 7));
            break;
        default:                            // FMOVECR, defined offsets and not
            nExtra = 0x5c00 | (nDest << 7) | ((x >> 43) & 0x3f);
            break;
        }
        s_nAt = i;
        Run (nOpcode, nExtra);

        for (int r = 0; r < 8; r++)
        {
            Fold (((uint64_t) fpu.registers[r].high << 48) ^ fpu.registers[r].low);
        }
        Fold (((uint64_t) fpu_get_fpsr () << 32) | fpu_get_fpcr ());
        for (int k = 0; k < 12; k++)
        {
            Fold (s_Memory[Operand + k]);
        }
    }
    return s_nHash;
}

static unsigned s_nFailures;

static void Expect (bool bOK, const char *pWhat)
{
    printf ("  %s %s\n", bOK ? "ok  " : "FAIL ", pWhat);
    if (!bOK)
    {
        s_nFailures++;
    }
}

int main (int argc, char **argv)
{
    MEMBaseDiff = (uintptr) s_Memory;
    signal (SIGALRM, Stuck);
    fpu_init (true);

    if (argc > 1)
    {
        const long n = atol (argv[1]);
        printf ("0x%016llxULL after %ld instructions\n", (unsigned long long) Stream (n), n);
        return 0;
    }

    printf ("68881 instructions\n");

    // The case that hung: FSIN.X (A0),FP7 of an unnormal near 2^3919, read
    // from memory as it is. It must return, and with what the normalised
    // value gives.
    static const uae_u8 Unnormal[12] = { 0x4f, 0x4e, 0x07, 0x5a, 0x46, 0xf1, 0x45, 0x0b,
                                         0x5f, 0x54, 0x2d, 0x32 };
    for (int k = 0; k < 12; k++)
    {
        s_Memory[Operand + k] = Unnormal[k];
    }
    m68k_areg (regs, 0) = Operand;
    fpu_set_fpcr (0);
    s_nAt = -1;
    Run (0xf210, 0x4b8e);
    const uae_u16 nHigh = fpu.registers[7].high;
    const uae_u64 nLow = fpu.registers[7].low;
    // The same value normalised by hand: the significand shifted left one
    // place, the exponent down one.
    s_Memory[Operand + 1] = 0x4d;
    s_Memory[Operand + 4] = 0x8d;
    s_Memory[Operand + 5] = 0xe2;
    s_Memory[Operand + 6] = 0x8a;
    s_Memory[Operand + 7] = 0x16;
    s_Memory[Operand + 8] = 0xbe;
    s_Memory[Operand + 9] = 0xa8;
    s_Memory[Operand + 10] = 0x5a;
    s_Memory[Operand + 11] = 0x64;
    Run (0xf210, 0x4b8e);
    Expect (fpu.registers[7].high == nHigh && fpu.registers[7].low == nLow,
            "FSIN of an unnormal returns, as for its normalised value");

    char Line[128];
    const long nCount = 1000000;
    const uint64_t nHash = Stream (nCount);
    snprintf (Line, sizeof Line, "%ld random instructions, state hash %016llx, %u illegal",
              nCount, (unsigned long long) nHash, s_nIllegal);
    Expect (nHash == 0xbdc039f865c5af9cULL && s_nIllegal == 0, Line);

    printf (s_nFailures ? "%u FAILED\n" : "all passed\n", s_nFailures);
    return s_nFailures ? 1 : 0;
}
