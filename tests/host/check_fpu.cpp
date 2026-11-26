/*
 * check_fpu.cpp — the 68881's arithmetic, on the development machine.
 *
 * The softfloat under fpu_softfloat.cpp needs nothing but 64-bit integers, so
 * it builds here unchanged and gives, by construction, the bits the board
 * gives. This checks the answers known independently (fpu_selftest.h says how
 * they were obtained) and pins the digest of a fixed stream of operations,
 * which the traced kernel compares against on the board.
 *
 *   ./check_fpu          the known answers and the digest
 *   ./check_fpu digest   print the digest, to pin a deliberate change
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>

#include "fpu_selftest.h"

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
    if (argc > 1 && strcmp (argv[1], "digest") == 0)
    {
        printf ("0x%016llxULL\n", (unsigned long long) FpuDigest (kFpuDigestCount));
        return 0;
    }

    printf ("68881 known answers\n");
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
        const floatx80 r = FpuApply (K.nOp, a, b, &Status);
        const bool bOK = r.high == K.rHigh && r.low == K.rLow
                      && Status.float_exception_flags == K.nFlags;
        Expect (bOK, K.pWhat);
        if (!bOK)
        {
            printf ("       got %04x %016llx flags %02x, expected %04x %016llx flags %02x\n",
                    r.high, (unsigned long long) r.low, Status.float_exception_flags,
                    K.rHigh, (unsigned long long) K.rLow, K.nFlags);
        }
    }

    char Line[128];
    unsigned nFirst = 0;
    floatx80 Got;
    uint8_t nFlags = 0;
    Expect (FpuCheckKnownAnswers (&nFirst, &Got, &nFlags) == s_nFailures,
            "the board's check agrees with this one");

    printf ("Fast paths\n");
    unsigned nTaken = 0;
    const unsigned nDiffer = FpuCheckFastPaths (2000000, &nTaken);
    snprintf (Line, sizeof Line, "add, sub, mul: %u of 6,000,000 taken, %u differ from softfloat",
              nTaken, nDiffer);
    Expect (nDiffer == 0 && nTaken > 4000000, Line);

    printf ("Same bits everywhere\n");
    const uint64_t nDigest = FpuDigest (kFpuDigestCount);
    snprintf (Line, sizeof Line, "digest of %u operations is %016llx", kFpuDigestCount,
              (unsigned long long) nDigest);
    Expect (nDigest == kFpuDigestExpected, Line);
    // Twice in a row: nothing in softfloat may keep state between calls that
    // changes an answer.
    Expect (FpuDigest (kFpuDigestCount) == nDigest, "the digest is repeatable");

    printf (s_nFailures ? "%u FAILED\n" : "all passed\n", s_nFailures);
    return s_nFailures ? 1 : 0;
}
