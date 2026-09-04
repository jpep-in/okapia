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

/*
 *  The caution mark
 *
 *  A thin triangle with rounded corners and an exclamation that fills it: the
 *  panel System 6 and 7 both showed, which is worth following because it is
 *  what the mark has always looked like on this machine and because it reads at
 *  a glance — a heavy outline with a small mark inside reads as a shape, not as
 *  a warning.
 *
 *  The mark is set in the typeface rather than built from a bar and a dot. A
 *  drawn one has to be re-tuned at every size and never quite matches the voice
 *  of the words beside it; the face already has the character, and the ladder
 *  has it at several sizes so nothing is magnified.
 */
void OkapiaPaintInfo (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                      TOkapiaColor)
{
    // The letter and nothing else. A ring around it was tried and it only added
    // weight: the mark already sits in a button, and a button inside a circle
    // inside a button is two frames doing one frame's work.
    //
    // The largest face that fits the height with a little air, asked for
    // largest first — the same rule the caution mark uses, for the same reason:
    // a letter chosen by a fraction of the height reads well at one size and
    // badly at the next.
    const int nRoom = (int) rRect.nHeight - (int) rRect.nHeight / 5;
    const TOkapiaFont *pFace = 0;
    for (int i = (int) OkapiaFaceCount - 1; i >= 0; i--)
    {
        if ((int) OkapiaFaces[i].pBold->nCapHeight <= nRoom)
        {
            pFace = OkapiaFaces[i].pBold;
            break;
        }
    }
    if (pFace == 0)
    {
        pFace = OkapiaFaces[0].pBold;   // the smallest there is, rather than nothing
    }

    // On the cap height, so it sits where the other marks sit: an "i" has a dot
    // above its cap and centring the whole ascent would push the stem low.
    const TRect Box = Rect (rRect.nX,
                            rRect.nY + ((int) rRect.nHeight - (int) pFace->nCapHeight) / 2,
                            rRect.nWidth, (unsigned) pFace->nCapHeight);
    GfxTextBox (pSurface, pFace, Box, "i", Ink, TextAlignCenter);
}

/*
 *  One weight for the three alert marks
 *
 *  They appear in the same place, one at a time, and a reader who sees them in
 *  turn sees the weight change rather than the shape. The triangle was drawn at
 *  a hairline and the balloon a third heavier; this is between them, and it is
 *  one number so that it cannot drift apart again.
 *
 *  The footer's marks are a family of their own — smaller, and never beside one
 *  of these — and keep their own weight.
 */
static int MarkStroke (int N)
{
    return N / 14 < 2 ? 2 : N / 14;
}

void OkapiaPaintNote (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                      TOkapiaColor)
{
    // A speech balloon with lines of writing in it. The face beside it in the
    // original is what does not survive sixteen pixels — and it is also the
    // part that would be quoting somebody else's drawing. The balloon alone
    // reads as "something is being said to you", which is the whole message.
    const int N = (int) rRect.nWidth;
    const int t = MarkStroke (N);
    const int nTail = N / 5 < 3 ? 3 : N / 5;

    // The balloon takes everything but the room its tail needs below.
    const TRect Balloon = Rect (rRect.nX, rRect.nY, rRect.nWidth,
                                (unsigned) ((int) rRect.nHeight - nTail));
    const int r = (int) Balloon.nHeight / 3;
    GfxRoundFrame (pSurface, Balloon, (unsigned) r, Ink, (unsigned) t);

    // The tail, drawn row by row from the balloon's lower edge: a wedge that
    // narrows as it goes down and leans left, the way a spoken one does.
    const int nBase = Balloon.nY + (int) Balloon.nHeight - t;
    const int nLeft = Balloon.nX + (int) Balloon.nWidth / 4;
    for (int i = 0; i <= nTail; i++)
    {
        const int nWide = nTail - i;
        if (nWide <= 0)
        {
            break;
        }
        GfxHLine (pSurface, nLeft, nBase + i, (unsigned) nWide, Ink);
    }

    // Three lines of writing, lighter than the balloon that holds them: writing
    // as heavy as its container is a black block, not a page.
    const int w = t / 2 < 1 ? 1 : t / 2;
    const TRect In = RectInset (Balloon, 3 * t, 2 * t);
    if (In.nWidth == 0 || (int) In.nHeight < 3 * w)
    {
        return;                         // too small to hold writing: the balloon says enough
    }
    for (int i = 0; i < 3; i++)
    {
        const int nY = In.nY + (int) In.nHeight * (2 * i + 1) / 6 - w / 2;
        const unsigned nLen = i == 2 ? In.nWidth * 3 / 5 : In.nWidth;
        GfxFill (pSurface, Rect (In.nX, nY, nLen, (unsigned) w), Ink);
    }
}

// An octagon, filled, row by row: a square whose four corners are cut away at
// forty-five degrees. There is no primitive for it and it does not want one —
// this is the only shape in the interface with eight sides.
static void FillOctagon (TSurface *pSurface, const TRect &rRect, int nCut,
                         TOkapiaColor Color)
{
    const int H = (int) rRect.nHeight;
    const int W = (int) rRect.nWidth;
    for (int y = 0; y < H; y++)
    {
        int nInset = 0;
        if (y < nCut)
        {
            nInset = nCut - y;
        }
        else if (y >= H - nCut)
        {
            nInset = y - (H - 1 - nCut);
        }
        if (2 * nInset >= W)
        {
            continue;
        }
        GfxHLine (pSurface, rRect.nX + nInset, rRect.nY + y,
                  (unsigned) (W - 2 * nInset), Color);
    }
}

void OkapiaPaintStop (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                      TOkapiaColor Back)
{
    // An octagon with a bar across it. The raised hand inside one is Apple's;
    // the octagon is everybody's, and it is the half that says "this does not
    // go on" without a word.
    const int N = (int) rRect.nWidth < (int) rRect.nHeight
                ? (int) rRect.nWidth : (int) rRect.nHeight;
    const int t = MarkStroke (N);
    const TRect Box = Rect (rRect.nX + ((int) rRect.nWidth - N) / 2,
                            rRect.nY + ((int) rRect.nHeight - N) / 2,
                            (unsigned) N, (unsigned) N);

    // The corners are taken off at the proportion that makes the eight sides
    // equal, which is what an octagon is — a rounded rectangle at that radius
    // is a lozenge, and reads as one.
    const int nCut = N * 29 / 100;
    FillOctagon (pSurface, Box, nCut, Ink);
    FillOctagon (pSurface, RectInset (Box, t, t), nCut - t < 0 ? 0 : nCut - t, Back);

    // The bar, across the interior and stopping inside the sides: a bar that
    // runs into the frame reads as a shape cut in two.
    const int nBarW = N - 2 * nCut;
    GfxFill (pSurface, Rect (Box.nX + (N - nBarW) / 2, Box.nY + (N - t) / 2,
                             (unsigned) nBarW, (unsigned) t), Ink);
}

void OkapiaPaintCaution (TSurface *pSurface, const TRect &rRect, TOkapiaColor Ink,
                         TOkapiaColor Back)
{
    (void) Back;
    const int N = (int) rRect.nWidth;
    // Barely rounded still — a generous radius reads as a shape rather than as
    // a warning — but at the weight the three marks share: this one was a
    // hairline beside a balloon a third heavier, and what one saw when they
    // followed each other was the weight changing.
    const int t = MarkStroke (N);
    const int r = N / 12 < 2 ? 2 : N / 12;

    GfxTriangleFrame (pSurface, rRect, (unsigned) r, Ink, (unsigned) t);

    /*
     *  The mark is fitted to the shape, not to a percentage of it
     *
     *  A triangle narrows towards its apex, so how tall a mark it can hold
     *  depends on how wide that mark is — and a fraction of the height that
     *  reads well at one size runs the stem into the sloping side at another.
     *  It did: at the size an alert actually uses, the bar met the left edge
     *  and the two merged, which looks exactly like half the mark being eaten.
     *
     *  So the largest face that fits is asked for, largest first, and fitting
     *  means both ends: clear of the base rule below, and inside the taper
     *  above.
     */
    const int nBase   = rRect.nY + (int) rRect.nHeight;
    const int nLowest = nBase - 2 * t - 1;      // clear of the base rule
    const int H = (int) rRect.nHeight;
    const int W = (int) rRect.nWidth;

    // Centred — but on the triangle's *visual* middle, which is not its
    // half-height. A triangle carries all its weight at the base, so a mark on
    // the geometric centre floats in the narrow part with a broad black field
    // beneath it and reads high; the eye puts the centre of such a shape down
    // near its centroid. Three sixths of the way down read high, four sixths
    // read low, and this is between them.
    //
    // Choosing the placement first also lets a larger face through: lower in
    // the triangle there is more room across, so the rule "the largest face
    // that can sit centred" answers one rung higher than it did.
    const int nMiddle = rRect.nY + H * 60 / 100;

    const TOkapiaFont *pFace = 0;
    int nTop = 0;
    for (int i = (int) OkapiaFaceCount - 1; i >= 0; i--)
    {
        const TOkapiaFont *p = OkapiaFaces[i].pBold;
        const int nCap = (int) p->nCapHeight;
        const int nInk = (int) GfxTextInkWidth (p, "!");

        // The highest the mark may start. The interior's half-width at a height
        // y is the outer taper less the stroke measured *across* rather than
        // along — a side this steep is wider in x than it is thick, nine
        // eighths of the stroke for a triangle as tall as it is wide — and two
        // pixels of air, one being not a gap but a coincidence. Solving that
        // for y is the highest the mark may start.
        const int nHighest = W > 0
            ? rRect.nY + (nInk / 2 + t * 9 / 8 + 2) * 2 * H / W
            : rRect.nY;
        const int nLatest = nLowest - nCap;

        const int y = nMiddle - nCap / 2;
        if (y >= nHighest && y <= nLatest)
        {
            pFace = p;
            nTop  = y;
            break;
        }
    }
    // Nothing could be centred: take the largest that fits at all rather than
    // leave the triangle empty, and let it sit where it can.
    for (int i = (int) OkapiaFaceCount - 1; pFace == 0 && i >= 0; i--)
    {
        const TOkapiaFont *p = OkapiaFaces[i].pBold;
        const int nCap = (int) p->nCapHeight;
        const int nInk = (int) GfxTextInkWidth (p, "!");
        const int nHighest = W > 0
            ? rRect.nY + (nInk / 2 + t * 9 / 8 + 2) * 2 * H / W
            : rRect.nY;
        const int nLatest = nLowest - nCap;
        if (nHighest <= nLatest)
        {
            pFace = p;
            nTop  = nHighest;
        }
    }
    if (pFace == 0)
    {
        return;                         // too small to hold a mark at all
    }

    const TRect Box = Rect (rRect.nX, nTop, rRect.nWidth,
                            (unsigned) pFace->nCapHeight);
    GfxTextBox (pSurface, pFace, Box, "!", Ink, TextAlignCenter);
}
