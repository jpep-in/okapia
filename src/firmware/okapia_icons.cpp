/*
 * okapia_icons.cpp — the marks the chrome wears, painted from their rectangle.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_icons.h"
#include "okapia_font.h"

/*
 *  Chrome icons, painted from the rectangle they are given
 *
 *  Drawn rather than stored, so they carry no size of their own: at twice the
 *  scale the ring has twice the pixels instead of twice the staircase. And
 *  nothing here is traced from any system, which is the rule the chrome was
 *  meant to keep.
 */

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
    // The stroke's mid-line radius, measured rather than assumed. GfxCircleFrame
    // works from a distance field taken from the rectangle's centre, and pixel
    // centres sit half a pixel further out — so the ink lands at
    // (width - t) / 2 + 1/2, exactly, at every size checked from sixteen pixels
    // to ninety-six. Computing it without that half pixel put the caps a whole
    // pixel off the arc wherever the division also truncated, which is why the
    // small sizes were the ones that showed it.
    const float R = ((float) Ring.nWidth - (float) t) / 2.0f + 0.5f;
    // Half the notch: the bar's half-width plus the air either side of it. A
    // single pixel of air is not a gap, it is a printing fault — the ring has to
    // read as open, a U rather than an O with something laid across it, and that
    // takes air one can see at the size the chrome is actually drawn.
    //
    // Written as a fraction of t alone it rounded down to exactly the bar's own
    // half-width at fourteen pixels and the bar welded itself to the ring, so
    // the floor is not decoration either.
    const int nAir = t * 3 / 4 < 2 ? 2 : t * 3 / 4;
    const int hn = t / 2 + nAir + t / 2;

    // The notch is cut down to where the arc actually reaches the width of the
    // opening, not merely through the top of the stroke. Cutting a band one
    // stroke deep left the arc standing between the two caps as soon as the
    // opening was widened — a slot in the ring with two marks beside it,
    // instead of a ring that is open.
    // Where the arc is actually cut, which is the edge of the notch and not its
    // centre line: the erase takes everything within hn + t/2 of the middle, so
    // the stroke survives from there outward and that is where its end is. Both
    // the depth of the cut and the caps that round it off are measured from
    // this — measured from hn instead, the caps sat a half stroke inside the
    // gap and floated free of the ring.
    const int hOpen = hn + t / 2;
    // Rounded, not truncated: at ninety-six pixels the true height was 23.8 and
    // a floor put the caps a whole pixel above the arc, so each one floated
    // free of the ring it was supposed to finish.
    const int ey = cy - (int) (__builtin_sqrtf (R * R - (float) (hOpen * hOpen)) + 0.5f);
    if (ey >= cy)
    {
        return;
    }

    // The notch is a wedge and not a rectangle, because the stroke has to be
    // cut *along a radius*. A vertical cut crosses the arc obliquely, so its
    // face is longer than the stroke is thick and no round cap can cover it —
    // a thin spur was left standing beyond each cap, which is what the artefact
    // was. Cut radially and the face is exactly one stroke wide, which is what
    // a disc of that diameter caps precisely.
    for (int y = rRect.nY; y <= cy; y++)
    {
        const int hw = hOpen * (cy - y) / (cy - ey);
        GfxHLine (pSurface, cx - hw, y, (unsigned) (2 * hw + 1), Back);
    }

    for (int nSide = -1; nSide <= 1; nSide += 2)
    {
        Capsule (pSurface, cx + nSide * hOpen - t / 2, ey - t / 2,
                 (unsigned) t, (unsigned) t, Ink);
    }

    Capsule (pSurface, cx - t / 2, rRect.nY, (unsigned) t,
             (unsigned) (cy - rRect.nY), Ink);
}

// An isoceles triangle, apex at the top, filled row by row. The outline is one
// of these with a smaller one taken back out: three thick strokes meeting at
// three corners is a great deal of arithmetic to get an even weight, and a
// difference of two fills has an even weight by construction.
static void Triangle (TSurface *pSurface, int nApexX, int nApexY, int nBaseY,
                      int nHalfBase, TOkapiaColor Color)
{
    const int nHeight = nBaseY - nApexY;
    if (nHeight <= 0)
    {
        return;
    }
    for (int y = 0; y <= nHeight; y++)
    {
        const int nHalf = nHalfBase * y / nHeight;
        GfxHLine (pSurface, nApexX - nHalf, nApexY + y, (unsigned) (2 * nHalf + 1), Color);
    }
}

void OkapiaPaintCaution (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                         TOkapiaColor Back)
{
    const int N = (int) rRect.nWidth;
    // Lighter than the chrome's other marks. A caution triangle is read as a
    // shape with something inside it, and at the weight the power ring wears it
    // closes up into a black lozenge with a notch.
    const int t = N / 14 < 2 ? 2 : N / 14;

    const int cx = rRect.nX + (int) rRect.nWidth / 2;
    const int y0 = rRect.nY + t / 2;
    const int y1 = rRect.nY + (int) rRect.nHeight - 1 - t / 2;
    const int hb = (int) rRect.nWidth / 2 - t / 2;
    const int H  = y1 - y0;
    if (H <= 0 || hb <= 0)
    {
        return;
    }

    Triangle (pSurface, cx, y0, y1, hb, Ink);

    // The inner triangle is the outer one offset inward by t *perpendicular to
    // each side*, which is not the same as inset by t on each axis. A corner as
    // sharp as this apex is far thicker along its bisector than across a side —
    // shifting it by a couple of strokes and taking the same off the width, as
    // this did, leaves a blunt point and an outline half again too heavy.
    const float d = __builtin_sqrtf ((float) (H * H + hb * hb));
    const int nApex = (int) ((float) t * d / (float) hb + 0.5f);
    const int nHalf = (int) ((float) hb * (float) (H - t) / (float) H
                             - (float) t * d / (float) H + 0.5f);
    if (nHalf <= 0 || y1 - t <= y0 + nApex)
    {
        return;
    }
    Triangle (pSurface, cx, y0 + nApex, y1 - t, nHalf, Back);

    /*
     *  The exclamation is the typeface's own
     *
     *  Drawn rather than constructed, because a bar and a square dot are a
     *  passable imitation of a mark the face already has, and the imitation has
     *  to be re-tuned at every size while the glyph is simply set. It also
     *  keeps the icon in the same voice as everything else on the screen.
     *
     *  The face is picked from the ladder for the room there is, so the mark is
     *  drawn at a size that exists rather than magnified — and, like the pixel
     *  icons, it stops growing once the largest rung is reached.
     */
    const int yA = y0 + nApex;                  // the interior's own apex
    const int yB = y1 - t;                      // and its base
    // The lower part of the wedge, where it is wide enough to set anything.
    const TRect Box = Rect (cx - nHalf, yA + (yB - yA) / 4,
                            (unsigned) (2 * nHalf),
                            (unsigned) ((yB - yA) - (yB - yA) / 4));
    if (Box.nHeight == 0)
    {
        return;
    }
    const TOkapiaFontSet *pFace = FontNearest (Box.nHeight);
    GfxTextBox (pSurface, pFace->pBold, Box, "!", Ink, TextAlignCenter);
}
