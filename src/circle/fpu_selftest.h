/*
 * fpu_selftest.h — the 68K softfloat checked the same way on the host and on the board.
 *
 * Two things are decidable about the FPU without a Macintosh. The first is
 * whether the arithmetic gives the right answer where the right answer is known
 * independently: each known answer below was computed in decimal at 200 digits,
 * not read back from softfloat, and each is a case the binary128 core it
 * replaces got wrong or could not express (FPCR's precision, directed rounding
 * to an integer, a tie, a value past 2^31). The second is whether it gives the
 * same bits everywhere: the digest runs a fixed stream of operations through
 * every rounding mode and precision and folds results and exception flags into
 * one number. tests/host/check_fpu pins that number on the development machine
 * and the traced kernel compares against the same constant on the board, so an
 * undefined behaviour that GCC and Clang compile differently, or a host that
 * reads the code otherwise, shows up as a different number.
 *
 * Header-only, so that the host test and kernel.cpp include the same code. It
 * needs the softfloat in the include path as fpu/softfloat/ (uae_cpu_2021).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_fpu_selftest_h
#define _okapia_fpu_selftest_h

#include <stdint.h>

extern "C" {
#include "fpu/softfloat/softfloat.h"
}

enum TFpuOp
{
    FpuAdd, FpuSub, FpuMul, FpuDiv, FpuSqrt, FpuToInt32, FpuToFloat64, FpuSin,
    FpuRem, FpuMod, FpuScale, FpuRoundToInt, FpuToFloat32, FpuCos, FpuTan,
    FpuAtan, FpuEtox, FpuLogn, FpuLog10, FpuTwotox,
    FpuOpCount
};

// Precision as softfloat counts it (80, 64, 32: extended, double, single), mode
// as FPCR's 0 to 3 (nearest, zero, minus, plus). An integer or a double
// result is in rLow, rHigh unused.
struct TFpuKnownAnswer
{
    const char *pWhat;
    uint8_t     nOp;
    uint8_t     nPrecision;
    uint8_t     nMode;
    uint16_t    aHigh;
    uint64_t    aLow;
    uint16_t    bHigh;
    uint64_t    bLow;
    uint16_t    rHigh;
    uint64_t    rLow;
    uint8_t     nFlags;     // softfloat's: 0x01 OPERR, 0x08 OVFL, 0x20 INEX2
};

static const TFpuKnownAnswer s_FpuKnownAnswers[] =
{
    // FMOVE.D of 1.0: the binary128 core set a fraction bit worth 2^-49.
    { "1.0 to a double", FpuToFloat64, 80, 0, 0x3fff, 0x8000000000000000ULL, 0, 0,
      0, 0x3ff0000000000000ULL, 0x00 },
    // To an integer in each mode: the binary128 core used (int)(x + 0.5).
    { "-1.75 to an integer, nearest", FpuToInt32, 80, 0, 0xbfff, 0xe000000000000000ULL, 0, 0,
      0, (uint64_t) (uint32_t) -2, 0x20 },
    { "2.5 to an integer, nearest (even)", FpuToInt32, 80, 0, 0x4000, 0xa000000000000000ULL, 0, 0,
      0, 2, 0x20 },
    { "3.5 to an integer, nearest (even)", FpuToInt32, 80, 0, 0x4000, 0xe000000000000000ULL, 0, 0,
      0, 4, 0x20 },
    { "-2.5 to an integer, towards minus", FpuToInt32, 80, 2, 0xc000, 0xa000000000000000ULL, 0, 0,
      0, (uint64_t) (uint32_t) -3, 0x20 },
    { "2.5 to an integer, towards plus", FpuToInt32, 80, 3, 0x4000, 0xa000000000000000ULL, 0, 0,
      0, 3, 0x20 },
    { "3e9 to an integer saturates, OPERR", FpuToInt32, 80, 0, 0x401e, 0xb2d05e0000000000ULL, 0, 0,
      0, 0x7fffffff, 0x01 },
    // 1/3 at FPCR's three precisions: the binary128 core ignored them.
    { "1/3 in extended", FpuDiv, 80, 0, 0x3fff, 0x8000000000000000ULL, 0x4000, 0xc000000000000000ULL,
      0x3ffd, 0xaaaaaaaaaaaaaaabULL, 0x20 },
    { "1/3 in double", FpuDiv, 64, 0, 0x3fff, 0x8000000000000000ULL, 0x4000, 0xc000000000000000ULL,
      0x3ffd, 0xaaaaaaaaaaaaa800ULL, 0x20 },
    { "1/3 in single", FpuDiv, 32, 0, 0x3fff, 0x8000000000000000ULL, 0x4000, 0xc000000000000000ULL,
      0x3ffd, 0xaaaaab0000000000ULL, 0x20 },
    { "1/3 in extended, towards zero", FpuDiv, 80, 1, 0x3fff, 0x8000000000000000ULL, 0x4000, 0xc000000000000000ULL,
      0x3ffd, 0xaaaaaaaaaaaaaaaaULL, 0x20 },
    // 1 + 2^-64 is exactly half a unit of 1.0: a tie, to even.
    { "1 + 2^-64 ties to 1", FpuAdd, 80, 0, 0x3fff, 0x8000000000000000ULL, 0x3fbf, 0x8000000000000000ULL,
      0x3fff, 0x8000000000000000ULL, 0x20 },
    { "sqrt(2)", FpuSqrt, 80, 0, 0x4000, 0x8000000000000000ULL, 0, 0,
      0x3fff, 0xb504f333f9de6484ULL, 0x20 },
    // Past 1e4932: the largest extended doubled overflows to infinity. OVFL
    // alone, not OVFL and INEX2 as IEEE 754 would have it: Motorola's accrued
    // rule (INEX = INEX1 or INEX2 or OVFL) is there because the exception byte
    // does not set INEX2 for an overflow.
    { "largest * 2 overflows", FpuMul, 80, 0, 0x7ffe, 0xffffffffffffffffULL, 0x4000, 0x8000000000000000ULL,
      0x7fff, 0x0000000000000000ULL, 0x08 },
    // A value a double cannot hold, kept: 2^-16000 * 2^-300.
    { "2^-16000 * 2^-300", FpuMul, 80, 0, 0x3fff - 16000, 0x8000000000000000ULL, 0x3fff - 300, 0x8000000000000000ULL,
      0x3fff - 16300, 0x8000000000000000ULL, 0x00 },
    // The FPSP's sine is not correctly rounded: the true value rounds to
    // ...7021, the 68040's algorithm gives one unit less. Pinned, not chosen.
    { "sin(1), the FPSP's answer", FpuSin, 80, 0, 0x3fff, 0x8000000000000000ULL, 0, 0,
      0x3ffe, 0xd76aa47848677020ULL, 0x20 },
};

static inline void FpuSetMode (float_status *pStatus, unsigned nPrecision, unsigned nMode)
{
    static const signed char s_Modes[4] =
    {
        float_round_nearest_even, float_round_to_zero, float_round_down, float_round_up
    };
    pStatus->float_detect_tininess = float_tininess_before_rounding;
    pStatus->float_rounding_mode = s_Modes[nMode & 3];
    pStatus->floatx80_rounding_precision = (signed char) nPrecision;
    pStatus->float_exception_flags = 0;
}

// One operation; an integer, single or double result comes back in low.
static inline floatx80 FpuApply (unsigned nOp, floatx80 a, floatx80 b, float_status *pStatus)
{
    floatx80 r;
    r.high = 0;
    r.low = 0;
    uint64_t q = 0;
    flag s = 0;
    switch (nOp)
    {
    case FpuAdd:        return floatx80_add (a, b, pStatus);
    case FpuSub:        return floatx80_sub (a, b, pStatus);
    case FpuMul:        return floatx80_mul (a, b, pStatus);
    case FpuDiv:        return floatx80_div (a, b, pStatus);
    case FpuSqrt:       return floatx80_sqrt (a, pStatus);
    case FpuToInt32:    r.low = (uint32_t) floatx80_to_int32 (a, pStatus);  return r;
    case FpuToFloat64:  r.low = floatx80_to_float64 (a, pStatus);           return r;
    case FpuSin:        return floatx80_sin (a, pStatus);
    case FpuRem:        r = floatx80_rem (a, b, &q, &s, pStatus); r.high ^= (uint16_t) (q << 1 | s); return r;
    case FpuMod:        r = floatx80_mod (a, b, &q, &s, pStatus); r.high ^= (uint16_t) (q << 1 | s); return r;
    case FpuScale:      return floatx80_scale (a, b, pStatus);
    case FpuRoundToInt: return floatx80_round_to_int (a, pStatus);
    case FpuToFloat32:  r.low = floatx80_to_float32 (a, pStatus);           return r;
    case FpuCos:        return floatx80_cos (a, pStatus);
    case FpuTan:        return floatx80_tan (a, pStatus);
    case FpuAtan:       return floatx80_atan (a, pStatus);
    case FpuEtox:       return floatx80_etox (a, pStatus);
    case FpuLogn:       return floatx80_logn (a, pStatus);
    case FpuLog10:      return floatx80_log10 (a, pStatus);
    default:            return floatx80_twotox (a, pStatus);
    }
}

// The known answers that failed; the first failure's index in *pFirst.
static inline unsigned FpuCheckKnownAnswers (unsigned *pFirst, floatx80 *pGot, uint8_t *pFlags)
{
    unsigned nFailed = 0;
    for (unsigned i = 0; i < sizeof s_FpuKnownAnswers / sizeof s_FpuKnownAnswers[0]; i++)
    {
        const TFpuKnownAnswer &K = s_FpuKnownAnswers[i];
        float_status Status = {};
        FpuSetMode (&Status, K.nPrecision, K.nMode);
        floatx80 a, b;
        a.high = K.aHigh;
        a.low = K.aLow;
        b.high = K.bHigh;
        b.low = K.bLow;
        floatx80 r = FpuApply (K.nOp, a, b, &Status);
        if (r.high == K.rHigh && r.low == K.rLow && Status.float_exception_flags == K.nFlags)
        {
            continue;
        }
        if (nFailed++ == 0)
        {
            *pFirst = i;
            *pGot = r;
            *pFlags = Status.float_exception_flags;
        }
    }
    return nFailed;
}

// An operand from a 64-bit stream: mostly normal values across the whole
// exponent range, and one in eight a zero, an infinity, a NaN, a denormal or
// an unnormal, signs included.
static inline floatx80 FpuOperand (uint64_t x)
{
    floatx80 f;
    const unsigned nKind = (unsigned) (x >> 61);
    const uint16_t nSign = (uint16_t) ((x >> 60) & 1) << 15;
    const uint64_t nMant = (x * 0x9e3779b97f4a7c15ULL) | (1ULL << 63);
    if (nKind != 0)
    {
        // Exponents near 1.0 most of the time, the full range the rest.
        uint16_t nExp = (x >> 57) & 1 ? (uint16_t) (0x3fff + (int) ((x >> 32) & 0x7f) - 64)
                                      : (uint16_t) (1 + (x >> 16) % 0x7ffe);
        f.high = nSign | nExp;
        f.low = nMant;
        return f;
    }
    switch ((x >> 56) & 7)
    {
    case 0:  f.high = nSign;          f.low = 0;                      break;  // zero
    case 1:  f.high = nSign | 0x7fff; f.low = 0;                      break;  // infinity
    case 2:  f.high = nSign | 0x7fff; f.low = nMant | (1ULL << 62);   break;  // quiet NaN
    case 3:  f.high = nSign | 0x7fff; f.low = (nMant & ~(1ULL << 62)) | 1; break; // signalling NaN
    case 4:  f.high = nSign;          f.low = nMant >> 1;             break;  // denormal
    case 5:  f.high = nSign | 0x3fff; f.low = nMant >> 3;             break;  // unnormal
    case 6:  f.high = nSign | 0x3fff; f.low = 1ULL << 63;             break;  // one
    default: f.high = nSign | 0x4000; f.low = 0xc90fdaa22168c235ULL;  break;  // pi
    }
    return f;
}

// The digest: nCount operations, each with fresh operands, every operation,
// mode and precision in turn, folded with FNV-1a.
static inline uint64_t FpuDigest (unsigned nCount)
{
    static const unsigned char s_Precisions[3] = { 80, 64, 32 };
    uint64_t nState = 0x0123456789abcdefULL;
    uint64_t nHash = 0xcbf29ce484222325ULL;
    for (unsigned i = 0; i < nCount; i++)
    {
        // splitmix64: the operands depend on nothing but i.
        nState += 0x9e3779b97f4a7c15ULL;
        uint64_t z = nState;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        const floatx80 a = FpuOperand (z);
        const floatx80 b = FpuOperand (z * 0xd1342543de82ef95ULL + 1);

        float_status Status = {};
        FpuSetMode (&Status, s_Precisions[i % 3], (i / 3) & 3);
        const floatx80 r = FpuApply ((i / 12) % FpuOpCount, a, b, &Status);

        const uint64_t Words[3] = { r.high, r.low, Status.float_exception_flags };
        for (unsigned w = 0; w < 3; w++)
        {
            for (unsigned nByte = 0; nByte < 8; nByte++)
            {
                nHash ^= (Words[w] >> (nByte * 8)) & 0xff;
                nHash *= 0x100000001b3ULL;
            }
        }
    }
    return nHash;
}

// What FpuDigest (kFpuDigestCount) gives, measured on the development machine
// (tests/host/check_fpu). A different number elsewhere is a different FPU.
static const unsigned kFpuDigestCount = 60000;
static const uint64_t kFpuDigestExpected = 0xb4c77e4ef889e204ULL;

#endif
