/*
 * okapia_icons.cpp — the marks the chrome wears, painted from their rectangle.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_icons.h"

/*
 *  Chrome icons, painted from the rectangle they are given
 *
 *  Drawn rather than stored, so they carry no size of their own: at twice the
 *  scale the ring has twice the pixels instead of twice the staircase. And
 *  nothing here is traced from any system, which is the rule the chrome was
 *  meant to keep.
 */

// Largest n with n*n <= v. Used to place the ring's rounded ends exactly on the
// arc rather than by eye, which is the difference between an icon that looks
// drawn and one that looks assembled.
static unsigned ISqrt (unsigned v)
{
    unsigned n = 0;
    while ((n + 1) * (n + 1) <= v)
    {
        n++;
    }
    return n;
}

// A stroke with round ends: a rounded rectangle whose radius is half its own
// thickness. Every line in these icons is one, which is what gives them the
// drawn look the references have and a bare rectangle has not.
static void Capsule (TSurface *pSurface, int nX, int nY, unsigned nW, unsigned nH,
                     TOkapiaColor Color)
{
    const unsigned nR = (nW < nH ? nW : nH) / 2;
    GfxRoundFill (pSurface, Rect (nX, nY, nW, nH), nR, Color);
}

void OkapiaPaintSettings (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                          TOkapiaColor Back)
{
    // Three rails, each with a knob at a different place along it. A row of
    // sliders says "settings" where a row of bullets says "list".
    // Everything is a fraction of the icon, with no floor anywhere: a knob with
    // a minimum size of seven pixels is fine at sixty and swallows its
    // neighbours at fourteen, which is what a 640x480 output actually gives.
    // Three knobs of k, spaced (N-k)/2, fit exactly and touch nothing.
    const int N = (int) rRect.nWidth;
    const int t = N / 12 < 1 ? 1 : N / 12;          // rail thickness
    const int k = N / 4 < 3 ? 3 : N / 4;            // knob side
    const int nStep = (N - k) / 2;
    const int x0 = rRect.nX;
    const int x1 = rRect.nX + N;

    static const int Knob[3] = { 66, 34, 62 };      // hundredths of the width

    for (int i = 0; i < 3; i++)
    {
        const int y = rRect.nY + k / 2 + i * nStep;
        Capsule (pSurface, x0, y - t / 2, (unsigned) (x1 - x0), (unsigned) t, Ink);

        int kx = rRect.nX + N * Knob[i] / 100 - k / 2;
        if (kx < x0)
        {
            kx = x0;
        }
        if (kx + k > x1)
        {
            kx = x1 - k;
        }
        const TRect Box = Rect (kx, y - k / 2, (unsigned) k, (unsigned) k);
        GfxRoundFill (pSurface, Box, (unsigned) (k / 4), Back);
        GfxRoundFrame (pSurface, Box, (unsigned) (k / 4), Ink, (unsigned) t);
    }
}

void OkapiaPaintPram (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink, TOkapiaColor)
{
    // A memory chip. The first version was the backup battery the parameter RAM
    // ran on: historically apt and unreadable at sixteen pixels, which is the
    // only examination an icon has to sit.
    const int N = (int) rRect.nWidth;
    const int nStroke = N / 12 < 1 ? 1 : N / 12;
    const TRect Body = RectInset (rRect, N / 5, N / 5);

    GfxFrame (pSurface, Body, Ink, (unsigned) nStroke);
    GfxFill (pSurface, RectInset (Body, (int) Body.nWidth / 4, (int) Body.nHeight / 4), Ink);

    const int nLegLen = N / 6 < 2 ? 2 : N / 6;
    for (int i = 0; i < 3; i++)
    {
        const int nY = Body.nY + (int) Body.nHeight * (i + 1) / 4 - nStroke / 2;
        GfxFill (pSurface, Rect (Body.nX - nLegLen, nY, (unsigned) nLegLen,
                                 (unsigned) nStroke), Ink);
        GfxFill (pSurface, Rect (Body.nX + (int) Body.nWidth, nY, (unsigned) nLegLen,
                                 (unsigned) nStroke), Ink);
    }
}

void OkapiaPaintPower (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                       TOkapiaColor Back)
{
    // One weight throughout, and round ends everywhere — the ring's two ends and
    // both ends of the bar. The bar starts above the ring and stops at its
    // centre; the notch is cut, then the ends are capped back on, placed on the
    // arc by arithmetic rather than by eye.
    const int N = (int) rRect.nWidth;
    const int t = N / 8 < 2 ? 2 : N / 8;
    const int nInset = t / 2;
    const TRect Ring = RectInset (rRect, nInset, nInset);

    GfxCircleFrame (pSurface, Ring, Ink, (unsigned) t);

    const int cx = Ring.nX + (int) Ring.nWidth / 2;
    const int cy = Ring.nY + (int) Ring.nHeight / 2;
    const int R  = ((int) Ring.nWidth - t) / 2;     // radius of the stroke's mid-line
    // Half the notch: the bar's half-width plus a clearance that is never zero.
    // Writing it as a fraction of t alone rounded down to exactly the bar's own
    // half-width at fourteen pixels, and the bar welded itself to the ring.
    const int nAir = t / 2 < 1 ? 1 : t / 2;
    const int hn = t / 2 + nAir + t / 2;

    GfxFill (pSurface, Rect (cx - hn - t / 2, Ring.nY - 1,
                             (unsigned) (2 * hn + t), (unsigned) (t + 2)), Back);

    const int ey = cy - (int) ISqrt ((unsigned) (R * R - hn * hn));
    for (int nSide = -1; nSide <= 1; nSide += 2)
    {
        Capsule (pSurface, cx + nSide * hn - t / 2, ey - t / 2,
                 (unsigned) t, (unsigned) t, Ink);
    }

    Capsule (pSurface, cx - t / 2, rRect.nY, (unsigned) t,
             (unsigned) (cy - rRect.nY), Ink);
}
