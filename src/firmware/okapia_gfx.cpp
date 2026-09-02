/*
 * okapia_gfx.cpp — surface and drawing primitives for the firmware.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_gfx.h"

#include <string.h>

// The palette. #BCBCBC is not a mid grey by accident: a black-and-white 50%
// checkerboard averages in *linear* light, so what the Macintosh desktop
// actually looked like is 0.5 linear, which is 188 in sRGB. Aiming at 0x80
// would give something much darker than the pattern it stands in for.
static const unsigned s_Palette[ColorCount] =
{
    0x000000,           // ColorBlack
    0xFFFFFF,           // ColorWhite
    0xBCBCBC,           // ColorGray
    0xE0E0E0,           // ColorLtGray
    0x9A9A9A,           // ColorDim
    0x4A4A4A            // ColorDark
};

unsigned GfxPaletteEntry (TOkapiaColor Color)
{
    return Color < ColorCount ? s_Palette[Color] : 0;
}

TRect RectInset (const TRect &rRect, int nDX, int nDY)
{
    TRect r;
    r.nX      = rRect.nX + nDX;
    r.nY      = rRect.nY + nDY;
    const int nW = (int) rRect.nWidth  - 2 * nDX;
    const int nH = (int) rRect.nHeight - 2 * nDY;
    r.nWidth  = nW > 0 ? (unsigned) nW : 0;
    r.nHeight = nH > 0 ? (unsigned) nH : 0;
    return r;
}

bool RectContains (const TRect &rRect, int nX, int nY)
{
    return    nX >= rRect.nX && nX < rRect.nX + (int) rRect.nWidth
           && nY >= rRect.nY && nY < rRect.nY + (int) rRect.nHeight;
}

// Every write goes through here, so clipping is stated once. A firmware that
// draws one pixel outside its canvas corrupts whatever follows it in memory,
// and the symptom would look like anything but a drawing bug.
static inline void Span (TSurface *pSurface, int nX, int nY, int nLength, unsigned nValue)
{
    if (nY < 0 || nY >= (int) pSurface->nHeight || nLength <= 0)
    {
        return;
    }
    if (nX < 0)
    {
        nLength += nX;
        nX = 0;
    }
    if (nX + nLength > (int) pSurface->nWidth)
    {
        nLength = (int) pSurface->nWidth - nX;
    }
    if (nLength <= 0)
    {
        return;
    }
    unsigned *p = (unsigned *) (pSurface->pPixels + (size_t) nY * pSurface->nPitch) + nX;
    for (int i = 0; i < nLength; i++)
    {
        p[i] = nValue;
    }
}

// The same lookup as GfxPaletteEntry, kept as one line so the table is read in
// one place only — two copies of a palette lookup is two places to forget.
static inline unsigned Value (TOkapiaColor Color)
{
    return GfxPaletteEntry (Color);
}

TRect RectUnion (const TRect &rA, const TRect &rB)
{
    if (rA.nWidth == 0 || rA.nHeight == 0)
    {
        return rB;
    }
    if (rB.nWidth == 0 || rB.nHeight == 0)
    {
        return rA;
    }
    const int nLeft   = rA.nX < rB.nX ? rA.nX : rB.nX;
    const int nTop    = rA.nY < rB.nY ? rA.nY : rB.nY;
    const int nRightA = rA.nX + (int) rA.nWidth,  nRightB = rB.nX + (int) rB.nWidth;
    const int nBotA   = rA.nY + (int) rA.nHeight, nBotB   = rB.nY + (int) rB.nHeight;
    const int nRight  = nRightA > nRightB ? nRightA : nRightB;
    const int nBottom = nBotA > nBotB ? nBotA : nBotB;
    return Rect (nLeft, nTop, (unsigned) (nRight - nLeft), (unsigned) (nBottom - nTop));
}

void GfxBlit (TSurface *pDst, const TSurface *pSrc, const TRect &rRect)
{
    int nX0 = rRect.nX < 0 ? 0 : rRect.nX;
    int nY0 = rRect.nY < 0 ? 0 : rRect.nY;
    int nX1 = rRect.nX + (int) rRect.nWidth;
    int nY1 = rRect.nY + (int) rRect.nHeight;
    if (nX1 > (int) pDst->nWidth)   nX1 = (int) pDst->nWidth;
    if (nY1 > (int) pDst->nHeight)  nY1 = (int) pDst->nHeight;

    for (int y = nY0; y < nY1; y++)
    {
        const unsigned *pIn = (const unsigned *) (pSrc->pPixels + (size_t) y * pSrc->nPitch);
        unsigned *pOut = (unsigned *) (pDst->pPixels + (size_t) y * pDst->nPitch);
        for (int x = nX0; x < nX1; x++)
        {
            pOut[x] = pIn[x];
        }
    }
}

void GfxClear (TSurface *pSurface, TOkapiaColor Color)
{
    for (unsigned y = 0; y < pSurface->nHeight; y++)
    {
        Span (pSurface, 0, (int) y, (int) pSurface->nWidth, Value (Color));
    }
}

void GfxFill (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color)
{
    for (unsigned y = 0; y < rRect.nHeight; y++)
    {
        Span (pSurface, rRect.nX, rRect.nY + (int) y, (int) rRect.nWidth, Value (Color));
    }
}

void GfxHLine (TSurface *pSurface, int nX, int nY, unsigned nLength, TOkapiaColor Color)
{
    Span (pSurface, nX, nY, (int) nLength, Value (Color));
}

void GfxVLine (TSurface *pSurface, int nX, int nY, unsigned nLength, TOkapiaColor Color)
{
    for (unsigned i = 0; i < nLength; i++)
    {
        Span (pSurface, nX, nY + (int) i, 1, Value (Color));
    }
}

void GfxFrame (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color, unsigned nThickness)
{
    if (nThickness == 0 || rRect.nWidth == 0 || rRect.nHeight == 0)
    {
        return;
    }
    if (nThickness * 2 >= rRect.nHeight || nThickness * 2 >= rRect.nWidth)
    {
        GfxFill (pSurface, rRect, Color);
        return;
    }
    for (unsigned i = 0; i < nThickness; i++)
    {
        GfxHLine (pSurface, rRect.nX, rRect.nY + (int) i, rRect.nWidth, Color);
        GfxHLine (pSurface, rRect.nX, rRect.nY + (int) (rRect.nHeight - 1 - i), rRect.nWidth, Color);
    }
    for (unsigned y = nThickness; y < rRect.nHeight - nThickness; y++)
    {
        Span (pSurface, rRect.nX, rRect.nY + (int) y, (int) nThickness, Value (Color));
        Span (pSurface, rRect.nX + (int) (rRect.nWidth - nThickness), rRect.nY + (int) y,
              (int) nThickness, Value (Color));
    }
}

/*
 *  Rounded shapes, drawn rather than stored
 *
 *  A signed distance field, not a table of corner insets. Every rounded shape
 *  here is one formula — the distance from a point to a rounded rectangle —
 *  and the distance is what gives the coverage of a pixel straddling the edge.
 *  That is antialiasing, and it is what removes the staircase from a curve that
 *  no amount of magnifying could.
 *
 *  This is the "vector" the interface needed, and it wanted no graphics
 *  processor: the shapes are computed at whatever size is asked for, so they
 *  are as smooth at 2.25 as at 1. Circle does carry a VideoCore port with
 *  OpenGL ES, but it is the old VideoCore IV blob — Pi 1 to 3 only, an enormous
 *  dependency, and no help at all with text.
 */

// Half a pixel each way, sampled on a 4x4 grid. Analytic coverage of a circle
// against a square is not worth the algebra at this size, and sixteen samples
// are already below what the eye separates.
static const int AA_SAMPLES = 4;

static float RoundBoxDistance (float x, float y, float hw, float hh, float r)
{
    // Distance from (x, y) — relative to the centre — to a rounded rectangle of
    // half-extents (hw, hh) and corner radius r. Negative inside.
    float dx = (x < 0.0f ? -x : x) - (hw - r);
    float dy = (y < 0.0f ? -y : y) - (hh - r);
    const float ax = dx > 0.0f ? dx : 0.0f;
    const float ay = dy > 0.0f ? dy : 0.0f;
    // __builtin_sqrtf, which is the FSQRT instruction on AArch64 and needs no
    // library. A hand-rolled Newton iteration stood here and was wrong: started
    // from the square and stopped after three steps, it answered 6.6 for 6, so
    // every outline came out about a pixel narrow — symmetrically, which is why
    // it read as "not enough clearance" rather than as a bug.
    const float outer = __builtin_sqrtf (ax * ax + ay * ay);
    const float inner = (dx > dy ? dx : dy);
    return (inner < 0.0f ? inner : outer) - r;
}

static inline void Blend (TSurface *pSurface, int nX, int nY, unsigned nColor, float fCoverage)
{
    if (   fCoverage <= 0.0f
        || nX < 0 || nX >= (int) pSurface->nWidth
        || nY < 0 || nY >= (int) pSurface->nHeight)
    {
        return;
    }
    unsigned *p = (unsigned *) (pSurface->pPixels + (size_t) nY * pSurface->nPitch) + nX;
    if (fCoverage >= 1.0f)
    {
        *p = nColor;
        return;
    }
    const unsigned nOld = *p;
    const unsigned a = (unsigned) (fCoverage * 256.0f + 0.5f);
    unsigned nOut = 0;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        const unsigned s = (nOld >> shift) & 0xFF;
        const unsigned d = (nColor >> shift) & 0xFF;
        nOut |= (((s * (256 - a) + d * a) >> 8) & 0xFF) << shift;
    }
    *p = nOut;
}

// One routine for every rounded shape: nThickness of 0 fills, anything else
// outlines. Having the fill and the outline share a formula is what keeps them
// on the same pixels — two formulas is how a filled button peeks out from under
// its own outline.
static void RoundShape (TSurface *pSurface, const TRect &rRect, float fRadius,
                        TOkapiaColor Color, float fThickness)
{
    if (rRect.nWidth == 0 || rRect.nHeight == 0)
    {
        return;
    }
    const unsigned nColor = Value (Color);
    // An outline lies *inside* the rectangle: its mid-line is half a stroke in,
    // so its outer edge falls exactly on the rectangle's edge and a one-pixel
    // stroke covers exactly one column. Centring the band on the edge instead
    // splits it over two columns at fifty percent each, which at 1:1 turns the
    // whole interface soft — invisible at twice the scale, glaring at one.
    const float shrink = fThickness * 0.5f;
    const float hw = (float) rRect.nWidth  * 0.5f - shrink;
    const float hh = (float) rRect.nHeight * 0.5f - shrink;
    float r = fRadius - shrink;
    const float lim = hw < hh ? hw : hh;
    if (r > lim)
    {
        r = lim;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }
    // The centre comes from the rectangle, never from the shrunken half-extent:
    // taking it from hw displaces the whole shape by half a stroke, to the left
    // and upwards. It shows as a ring that clears its button by one pixel more
    // on the left than on the right — which is invisible until it is measured,
    // and unmistakable once it is.
    const float cx = (float) rRect.nX + (float) rRect.nWidth  * 0.5f;
    const float cy = (float) rRect.nY + (float) rRect.nHeight * 0.5f;
    const float step = 1.0f / (float) AA_SAMPLES;
    const float weight = 1.0f / (float) (AA_SAMPLES * AA_SAMPLES);

    for (unsigned row = 0; row < rRect.nHeight; row++)
    {
        const int nY = rRect.nY + (int) row;
        int nRunStart = -1;

        for (unsigned col = 0; col <= rRect.nWidth; col++)
        {
            float fCoverage = 0.0f;
            if (col < rRect.nWidth)
            {
                const int nX = rRect.nX + (int) col;
                // The centre first: away from any edge, one sample settles it
                // and the sixteen are never taken.
                const float d0 = RoundBoxDistance ((float) nX + 0.5f - cx,
                                                   (float) nY + 0.5f - cy, hw, hh, r);
                // The shape has already been shrunk by half a stroke, so its
                // boundary *is* the outline's mid-line and the band is simply
                // |d| <= t/2. Adding the half-stroke again before taking the
                // absolute value — which is what stood here — puts the whole
                // band a half-pixel outside where it belongs, on every side.
                const float e0 = fThickness > 0.0f
                               ? ((d0 < 0.0f ? -d0 : d0) - fThickness * 0.5f)
                               : d0;
                if (e0 < -1.0f)
                {
                    fCoverage = 1.0f;
                }
                else if (e0 > 1.0f)
                {
                    fCoverage = 0.0f;
                }
                else
                {
                    for (int sy = 0; sy < AA_SAMPLES; sy++)
                    {
                        for (int sx = 0; sx < AA_SAMPLES; sx++)
                        {
                            const float px = (float) nX + ((float) sx + 0.5f) * step;
                            const float py = (float) nY + ((float) sy + 0.5f) * step;
                            float d = RoundBoxDistance (px - cx, py - cy, hw, hh, r);
                            if (fThickness > 0.0f)
                            {
                                if (d < 0.0f)
                                {
                                    d = -d;
                                }
                                d -= fThickness * 0.5f;
                            }
                            if (d <= 0.0f)
                            {
                                fCoverage += weight;
                            }
                        }
                    }
                }
            }

            if (fCoverage >= 1.0f && nRunStart < 0)
            {
                nRunStart = (int) col;
            }
            else if (fCoverage < 1.0f)
            {
                if (nRunStart >= 0)
                {
                    Span (pSurface, rRect.nX + nRunStart, nY, (int) col - nRunStart, nColor);
                    nRunStart = -1;
                }
                if (col < rRect.nWidth)
                {
                    Blend (pSurface, rRect.nX + (int) col, nY, nColor, fCoverage);
                }
            }
        }
    }
}

void GfxRoundFill (TSurface *pSurface, const TRect &rRect, unsigned nRadius, TOkapiaColor Color)
{
    RoundShape (pSurface, rRect, (float) nRadius, Color, 0.0f);
}

void GfxRoundFrame (TSurface *pSurface, const TRect &rRect, unsigned nRadius,
                    TOkapiaColor Color, unsigned nThickness)
{
    if (nThickness == 0)
    {
        return;
    }
    RoundShape (pSurface, rRect, (float) nRadius, Color, (float) nThickness);
}

/*
 *  Triangles
 *
 *  Signed distance to a triangle, negative inside: the nearest point on each
 *  edge clamped to the segment, and the winding to decide the sign. It is the
 *  formulation everybody uses because it is exact everywhere, corners included,
 *  which is the whole reason the corners can then be rounded by subtracting a
 *  radius instead of being drawn.
 */
static float TriangleDistance (float px, float py,
                               float ax, float ay, float bx, float by,
                               float cx, float cy)
{
    const float e0x = bx - ax, e0y = by - ay;
    const float e1x = cx - bx, e1y = cy - by;
    const float e2x = ax - cx, e2y = ay - cy;
    const float v0x = px - ax, v0y = py - ay;
    const float v1x = px - bx, v1y = py - by;
    const float v2x = px - cx, v2y = py - cy;

    float t0 = (v0x * e0x + v0y * e0y) / (e0x * e0x + e0y * e0y);
    float t1 = (v1x * e1x + v1y * e1y) / (e1x * e1x + e1y * e1y);
    float t2 = (v2x * e2x + v2y * e2y) / (e2x * e2x + e2y * e2y);
    t0 = t0 < 0.0f ? 0.0f : (t0 > 1.0f ? 1.0f : t0);
    t1 = t1 < 0.0f ? 0.0f : (t1 > 1.0f ? 1.0f : t1);
    t2 = t2 < 0.0f ? 0.0f : (t2 > 1.0f ? 1.0f : t2);

    const float p0x = v0x - e0x * t0, p0y = v0y - e0y * t0;
    const float p1x = v1x - e1x * t1, p1y = v1y - e1y * t1;
    const float p2x = v2x - e2x * t2, p2y = v2y - e2y * t2;

    const float s = e0x * e2y - e0y * e2x < 0.0f ? -1.0f : 1.0f;

    float dx = p0x * p0x + p0y * p0y;
    float dy = s * (v0x * e0y - v0y * e0x);
    const float d1x = p1x * p1x + p1y * p1y;
    const float d1y = s * (v1x * e1y - v1y * e1x);
    const float d2x = p2x * p2x + p2y * p2y;
    const float d2y = s * (v2x * e2y - v2y * e2x);
    if (d1x < dx) dx = d1x;
    if (d1y < dy) dy = d1y;
    if (d2x < dx) dx = d2x;
    if (d2y < dy) dy = d2y;

    return dy < 0.0f ? __builtin_sqrtf (dx) : -__builtin_sqrtf (dx);
}

void GfxTriangleFrame (TSurface *pSurface, const TRect &rRect, unsigned nRadius,
                       TOkapiaColor Color, unsigned nThickness)
{
    if (rRect.nWidth < 3 || rRect.nHeight < 3)
    {
        return;
    }
    const unsigned nColor = Value (Color);
    const float t = (float) nThickness;

    // The triangle the outline's *outer* edge follows, and then the one whose
    // distance field is measured: inset by the corner radius, since a rounded
    // corner is that shape's distance field taken out to the radius again.
    const float ax = (float) rRect.nX + (float) rRect.nWidth * 0.5f;
    const float ay = (float) rRect.nY;
    const float bx = (float) rRect.nX + (float) rRect.nWidth;
    const float by = (float) rRect.nY + (float) rRect.nHeight;
    const float cx0 = (float) rRect.nX;
    const float cy0 = by;

    // Insetting a triangle by r means moving every edge inward by r, which is
    // a scale about the incentre by (rho - r) / rho. Moving the vertices toward
    // the centroid instead is the obvious version and insets the three edges by
    // three different amounts, so the corners come out unequal.
    const float la = __builtin_sqrtf ((bx - cx0) * (bx - cx0) + (by - cy0) * (by - cy0));
    const float lb = __builtin_sqrtf ((cx0 - ax) * (cx0 - ax) + (cy0 - ay) * (cy0 - ay));
    const float lc = __builtin_sqrtf ((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
    const float per = la + lb + lc;
    if (per <= 0.0f)
    {
        return;
    }
    const float ix = (la * ax + lb * bx + lc * cx0) / per;
    const float iy = (la * ay + lb * by + lc * cy0) / per;
    const float area = 0.5f * __builtin_fabsf ((bx - ax) * (cy0 - ay) - (cx0 - ax) * (by - ay));
    const float rho = area / (per * 0.5f);

    float r = (float) nRadius;
    if (r > rho - 1.0f)
    {
        r = rho - 1.0f;
    }
    if (r < 0.0f)
    {
        r = 0.0f;
    }
    const float k = rho > 0.0f ? (rho - r) / rho : 0.0f;
    const float Ax = ix + (ax - ix) * k,   Ay = iy + (ay - iy) * k;
    const float Bx = ix + (bx - ix) * k,   By = iy + (by - iy) * k;
    const float Cx = ix + (cx0 - ix) * k,  Cy = iy + (cy0 - iy) * k;

    // The band, or the fill. As with the rounded box, the outline lies inside
    // the shape: its outer edge falls on the triangle rather than straddling it.
    const float band = t * 0.5f;
    const float edge = r - band;
    const float step = 1.0f / (float) AA_SAMPLES;
    const float weight = 1.0f / (float) (AA_SAMPLES * AA_SAMPLES);

    for (unsigned row = 0; row < rRect.nHeight; row++)
    {
        const int nY = rRect.nY + (int) row;
        for (unsigned col = 0; col < rRect.nWidth; col++)
        {
            const int nX = rRect.nX + (int) col;
            float fCoverage = 0.0f;
            for (unsigned sy = 0; sy < AA_SAMPLES; sy++)
            {
                for (unsigned sx = 0; sx < AA_SAMPLES; sx++)
                {
                    const float fx = (float) nX + ((float) sx + 0.5f) * step;
                    const float fy = (float) nY + ((float) sy + 0.5f) * step;
                    const float d = TriangleDistance (fx, fy, Ax, Ay, Bx, By, Cx, Cy);
                    const bool bIn = nThickness == 0
                                         ? d - r <= 0.0f
                                         : (d - edge < 0.0f ? edge - d : d - edge) <= band;
                    if (bIn)
                    {
                        fCoverage += weight;
                    }
                }
            }
            Blend (pSurface, nX, nY, nColor, fCoverage);
        }
    }
}

void GfxCircleFill (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color)
{
    const unsigned n = rRect.nWidth < rRect.nHeight ? rRect.nWidth : rRect.nHeight;
    RoundShape (pSurface, rRect, (float) n * 0.5f, Color, 0.0f);
}

void GfxCircleFrame (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color,
                     unsigned nThickness)
{
    GfxRoundFrame (pSurface, rRect, (rRect.nWidth < rRect.nHeight
                                     ? rRect.nWidth : rRect.nHeight) / 2, Color, nThickness);
}

void GfxInvert (TSurface *pSurface, const TRect &rRect)
{
    const unsigned nBlack = Value (ColorBlack);
    const unsigned nWhite = Value (ColorWhite);

    for (unsigned y = 0; y < rRect.nHeight; y++)
    {
        const int nY = rRect.nY + (int) y;
        if (nY < 0 || nY >= (int) pSurface->nHeight)
        {
            continue;
        }
        unsigned *p = (unsigned *) (pSurface->pPixels + (size_t) nY * pSurface->nPitch);
        for (unsigned x = 0; x < rRect.nWidth; x++)
        {
            const int nX = rRect.nX + (int) x;
            if (nX < 0 || nX >= (int) pSurface->nWidth)
            {
                continue;
            }
            if (p[nX] == nBlack)
            {
                p[nX] = nWhite;
            }
            else if (p[nX] == nWhite)
            {
                p[nX] = nBlack;
            }
        }
    }
}

/*
 *  Text
 */

// -1 when the face has no such glyph.
static int GlyphIndex (const TOkapiaFont *pFont, int nCode)
{
    if (nCode >= (int) pFont->nFirst && nCode <= (int) pFont->nLast)
    {
        return nCode - (int) pFont->nFirst;
    }
    for (unsigned i = 0; i < pFont->nExtra; i++)
    {
        if (pFont->pExtraCode[i] == (unsigned short) nCode)
        {
            return (int) (pFont->nLast - pFont->nFirst + 1 + i);
        }
    }
    return -1;
}

/*
 *  Sources are UTF-8, the faces are indexed by code point
 *
 *  Decoding here rather than storing Latin-1 literals keeps the translations in
 *  a form editors and diffs agree about, and removes a quiet trap: a "\xE9"
 *  literal swallows the letter after it when that letter is a hex digit.
 *
 *  The face carries Latin-1 plus the marks a French translation reaches for and
 *  ISO-8859-1 lacks — the curly apostrophe, the dashes, the ellipsis, oe — so
 *  nothing has to be folded to an ASCII ancestor any more.
 */
static int NextChar (const unsigned char **pp)
{
    const unsigned char *p = *pp;
    if (*p == '\0')
    {
        return -1;
    }

    unsigned nCode;
    if (*p < 0x80)
    {
        nCode = *p++;
    }
    else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
    {
        nCode = (unsigned) ((*p & 0x1F) << 6) | (p[1] & 0x3F);
        p += 2;
    }
    else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
    {
        nCode = (unsigned) ((*p & 0x0F) << 12) | (unsigned) ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        p += 3;
    }
    else
    {
        *pp = p + 1;                    // not UTF-8: skip the byte, draw nothing
        return 0;
    }
    *pp = p;

    return nCode == 0x00A0 ? ' ' : (int) nCode;
}

static unsigned GfxTextWidthRun (const unsigned char *p, const unsigned char *pEnd,
                                 const TOkapiaFont *pFont)
{
    unsigned nWidth = 0;
    while (pEnd == 0 || p < pEnd)
    {
        const int nCode = NextChar (&p);
        if (nCode < 0)
        {
            break;
        }
        const int nIndex = GlyphIndex (pFont, nCode);
        if (nIndex >= 0)
        {
            nWidth += pFont->pWidth[nIndex];
        }
    }
    return nWidth;
}

unsigned GfxTextWidth (const TOkapiaFont *pFont, const char *pText)
{
    unsigned nWidth = 0;
    const unsigned char *p = (const unsigned char *) pText;

    for (int nCode = NextChar (&p); nCode >= 0; nCode = NextChar (&p))
    {
        const int nIndex = GlyphIndex (pFont, nCode);
        if (nIndex < 0)
        {
            continue;
        }
        nWidth += pFont->pWidth[nIndex];
    }
    return nWidth;
}

// Draws from p up to but not including pEnd, or to the terminator when pEnd is
// zero. The bound is what lets a box draw a prefix without copying it anywhere:
// a firmware that allocates nothing has no scratch string to truncate into.
static void TextRun (TSurface *pSurface, const TOkapiaFont *pFont, int nX, int nY,
                     const unsigned char *p, const unsigned char *pEnd, TOkapiaColor Color)
{
    const unsigned nStride = pFont->nBytesPerRow;
    const unsigned nBits   = nStride * 8;

    while (pEnd == 0 || p < pEnd)
    {
        const int nCode = NextChar (&p);
        if (nCode < 0)
        {
            break;
        }
        const int nIndex = GlyphIndex (pFont, nCode);
        if (nIndex < 0)
        {
            continue;
        }
        const unsigned char *pBits = pFont->pBits
                                   + (size_t) nIndex * pFont->nHeight * nStride;

        for (unsigned row = 0; row < pFont->nHeight; row++)
        {
            const unsigned char *pRow = pBits + row * nStride;
            int nRunStart = -1;

            for (unsigned bit = 0; bit <= nBits; bit++)
            {
                bool bInk = false;
                if (bit < nBits)
                {
                    bInk = (pRow[bit >> 3] & (0x80u >> (bit & 7))) != 0;
                }
                // Runs rather than pixels: a 34-pixel face would otherwise pay
                // a bounds check per bit, and a whole screen of text is the one
                // place this loop is hot.
                if (bInk && nRunStart < 0)
                {
                    nRunStart = (int) bit;
                }
                else if (!bInk && nRunStart >= 0)
                {
                    Span (pSurface, nX + nRunStart, nY + (int) row,
                          (int) bit - nRunStart, Value (Color));
                    nRunStart = -1;
                }
            }
        }
        nX += (int) pFont->pWidth[nIndex];
    }
}

void GfxText (TSurface *pSurface, const TOkapiaFont *pFont, int nX, int nY,
              const char *pText, TOkapiaColor Color)
{
    TextRun (pSurface, pFont, nX, nY, (const unsigned char *) pText, 0, Color);
}

unsigned GfxTextWidthUpTo (const TOkapiaFont *pFont, const char *pText, unsigned nBytes)
{
    const unsigned char *p = (const unsigned char *) pText;
    return GfxTextWidthRun (p, p + nBytes, pFont);
}

void GfxImage (TSurface *pSurface, const TGlyphImage *pImage, int nX, int nY,
               TOkapiaColor Color, unsigned nScale)
{
    if (nScale == 0)
    {
        nScale = 1;
    }
    const unsigned nValue = Value (Color);

    for (unsigned y = 0; y < pImage->nHeight; y++)
    {
        const unsigned nRow = pImage->pRows[y];
        unsigned x = 0;
        while (x < pImage->nWidth)
        {
            if (!(nRow & (1u << (pImage->nWidth - 1 - x))))
            {
                x++;
                continue;
            }
            unsigned nRun = 1;
            while (x + nRun < pImage->nWidth && (nRow & (1u << (pImage->nWidth - 1 - x - nRun))))
            {
                nRun++;
            }
            for (unsigned k = 0; k < nScale; k++)
            {
                Span (pSurface, nX + (int) (x * nScale), nY + (int) (y * nScale + k),
                      (int) (nRun * nScale), nValue);
            }
            x += nRun;
        }
    }
}

int GfxTextTop (const TOkapiaFont *pFont, const TRect &rRect)
{
    // The optical centre of a line is its capitals, not its cell: the cell
    // carries a descent that "Démarrer" barely uses and "Focus" not at all, so
    // centring on it sits every label a pixel or two high. The baseline is
    // placed so the cap box is centred, and the top follows from the ascent.
    const int nBaseline = rRect.nY
                        + ((int) rRect.nHeight + (int) pFont->nCapHeight) / 2;
    return nBaseline - (int) pFont->nAscent;
}

/*
 *  Text in a box
 *
 *  Every label in the chrome goes through here, which is what makes "no text
 *  leaves its control" true by construction rather than by everyone
 *  remembering. What will not fit is cut and finished with an ellipsis, the way
 *  TruncString did — a label clipped mid-letter reads as a drawing fault, where
 *  one that ends in three dots says plainly that there is more.
 */

// U+2026. The faces carry it; a face that did not would silently truncate
// without saying so, which is why the width is measured rather than assumed.
static const char ELLIPSIS[] = "\xE2\x80\xA6";

// The last byte position at which the text can be cut so that what precedes it,
// plus the ellipsis, still fits nMax. Answers 0 when not even that fits.
static const unsigned char *TextCut (const TOkapiaFont *pFont, const unsigned char *pText,
                                     unsigned nMax)
{
    const unsigned nDots = GfxTextWidth (pFont, ELLIPSIS);
    if (nDots > nMax)
    {
        return 0;
    }
    const unsigned nRoom = nMax - nDots;

    const unsigned char *p = pText;
    const unsigned char *pGood = pText;
    unsigned nWidth = 0;
    for (;;)
    {
        const int nCode = NextChar (&p);
        if (nCode < 0)
        {
            return p;                   // the whole string fits inside nRoom
        }
        const int nIndex = GlyphIndex (pFont, nCode);
        if (nIndex >= 0)
        {
            nWidth += pFont->pWidth[nIndex];
        }
        if (nWidth > nRoom)
        {
            return pGood;
        }
        pGood = p;
    }
}

unsigned GfxTextBox (TSurface *pSurface, const TOkapiaFont *pFont, const TRect &rBox,
                     const char *pText, TOkapiaColor Color, TTextAlign Align)
{
    if (pText == 0)
    {
        return 0;
    }
    const int nY = GfxTextTop (pFont, rBox);
    const unsigned nWidth = GfxTextWidth (pFont, pText);

    if (nWidth <= rBox.nWidth)
    {
        int nX = rBox.nX;
        if (Align == TextAlignCenter)
        {
            nX += ((int) rBox.nWidth - (int) nWidth) / 2;
        }
        else if (Align == TextAlignRight)
        {
            nX += (int) rBox.nWidth - (int) nWidth;
        }
        TextRun (pSurface, pFont, nX, nY, (const unsigned char *) pText, 0, Color);
        return nWidth;
    }

    const unsigned char *pCut = TextCut (pFont, (const unsigned char *) pText, rBox.nWidth);
    if (pCut == 0)
    {
        return 0;                       // narrower than an ellipsis: draw nothing
    }
    // Left-aligned whatever was asked for: a truncated label centred in its box
    // leaves a gap on the left and its ellipsis short of the right edge, which
    // reads as a mistake twice over.
    TextRun (pSurface, pFont, rBox.nX, nY, (const unsigned char *) pText, pCut, Color);
    const unsigned nDrawn = GfxTextWidthRun ((const unsigned char *) pText, pCut, pFont);
    TextRun (pSurface, pFont, rBox.nX + (int) nDrawn, nY,
             (const unsigned char *) ELLIPSIS, 0, Color);
    return nDrawn + GfxTextWidth (pFont, ELLIPSIS);
}

unsigned GfxTextOffsetAt (const TOkapiaFont *pFont, const char *pText, int nX)
{
    if (pText == 0 || nX <= 0)
    {
        return 0;
    }
    const unsigned char *pStart = (const unsigned char *) pText;
    const unsigned char *p = pStart;
    int nWidth = 0;
    for (;;)
    {
        const unsigned char *pBefore = p;
        const int nCode = NextChar (&p);
        if (nCode < 0)
        {
            return (unsigned) (pBefore - pStart);
        }
        const int nIndex = GlyphIndex (pFont, nCode);
        const int nAdvance = nIndex < 0 ? 0 : (int) pFont->pWidth[nIndex];
        if (nX < nWidth + nAdvance / 2)
        {
            return (unsigned) (pBefore - pStart);
        }
        nWidth += nAdvance;
    }
}

bool GfxTextFits (const TOkapiaFont *pFont, const TRect &rBox, const char *pText)
{
    return pText == 0 || GfxTextWidth (pFont, pText) <= rBox.nWidth;
}

/*
 *  The pointer
 *
 *  The arrow is the shape alone; its white surround is computed by dilating it,
 *  the same habit as the rest of the chrome — one bitmap cannot fall out of step
 *  with a second one that does not exist. A cursor without that surround
 *  disappears the moment it crosses black text, which is most of a dialogue.
 */

static const unsigned CURSOR_W = 12;
static const unsigned CURSOR_H = 16;

static const unsigned short s_ArrowRows[CURSOR_H] =
{
    0x800, 0xC00, 0xE00, 0xF00, 0xF80, 0xFC0, 0xFE0, 0xFF0,
    0xFF8, 0xFC0, 0xEC0, 0xC60, 0x860, 0x030, 0x030, 0x000
};

// The beam: a stem with a serif at each end, so that it stays findable against
// a line of text — which is the whole reason it has them.
// The serifs run either side of the stem without a break in them: written as
// two short marks with a gap, the beam came out lopsided and looked broken.
static const unsigned short s_BeamRows[CURSOR_H] =
{
    0x000, 0x3E0, 0x080, 0x080, 0x080, 0x080, 0x080, 0x080,
    0x080, 0x080, 0x080, 0x080, 0x080, 0x080, 0x3E0, 0x000
};

static const unsigned short *s_pCursorRows = s_ArrowRows;

static bool CursorInk (int nX, int nY)
{
    if (nX < 0 || nY < 0 || nX >= (int) CURSOR_W || nY >= (int) CURSOR_H)
    {
        return false;
    }
    return (s_pCursorRows[nY] & (1 << (CURSOR_W - 1 - nX))) != 0;
}

// The largest whole scale the interface ever asks of an icon. The save-under is
// a static because the firmware allocates nothing once it is running, so its
// size has to be decided here rather than discovered.
static const unsigned CURSOR_MAX_SCALE = 4;
static const unsigned CURSOR_MAX_W = (CURSOR_W + 2) * CURSOR_MAX_SCALE;
static const unsigned CURSOR_MAX_H = (CURSOR_H + 2) * CURSOR_MAX_SCALE;

static unsigned s_Under[CURSOR_MAX_W * CURSOR_MAX_H];
static TRect    s_UnderRect;
static bool     s_bCursorShown;

TRect GfxCursorHide (TSurface *pSurface)
{
    if (!s_bCursorShown)
    {
        return Rect (0, 0, 0, 0);
    }
    for (unsigned y = 0; y < s_UnderRect.nHeight; y++)
    {
        const int nY = s_UnderRect.nY + (int) y;
        if (nY < 0 || nY >= (int) pSurface->nHeight)
        {
            continue;
        }
        unsigned *pRow = (unsigned *) (pSurface->pPixels + (size_t) nY * pSurface->nPitch);
        for (unsigned x = 0; x < s_UnderRect.nWidth; x++)
        {
            const int nX = s_UnderRect.nX + (int) x;
            if (nX < 0 || nX >= (int) pSurface->nWidth)
            {
                continue;
            }
            pRow[nX] = s_Under[y * CURSOR_MAX_W + x];
        }
    }
    s_bCursorShown = false;
    return s_UnderRect;
}

TRect GfxCursorShow (TSurface *pSurface, int nX, int nY, unsigned nScale,
                     TGfxCursor Shape)
{
    const TRect Was = GfxCursorHide (pSurface);

    // The point each shape actually points with: the arrow's tip is its corner,
    // the beam's is the middle of its stem. Drawing both from the same corner
    // puts the beam's click half a character to the right of where it looked.
    s_pCursorRows = Shape == GfxCursorBeam ? s_BeamRows : s_ArrowRows;
    if (Shape == GfxCursorBeam)
    {
        nX -= (int) (4 * nScale);
        nY -= (int) (8 * nScale);
    }

    if (nScale < 1)
    {
        nScale = 1;
    }
    if (nScale > CURSOR_MAX_SCALE)
    {
        nScale = CURSOR_MAX_SCALE;
    }

    // One bitmap pixel of margin all round, which is where the white surround
    // goes: the arrow's own tip is at 0,0 and the surround is outside it.
    const TRect Box = Rect (nX - (int) nScale, nY - (int) nScale,
                            (CURSOR_W + 2) * nScale, (CURSOR_H + 2) * nScale);
    s_UnderRect = Box;

    const unsigned nBlack = GfxPaletteEntry (ColorBlack);
    const unsigned nWhite = GfxPaletteEntry (ColorWhite);

    for (unsigned y = 0; y < Box.nHeight; y++)
    {
        const int nDstY = Box.nY + (int) y;
        if (nDstY < 0 || nDstY >= (int) pSurface->nHeight)
        {
            continue;
        }
        unsigned *pRow = (unsigned *) (pSurface->pPixels + (size_t) nDstY * pSurface->nPitch);
        const int nSrcY = (int) (y / nScale) - 1;
        for (unsigned x = 0; x < Box.nWidth; x++)
        {
            const int nDstX = Box.nX + (int) x;
            if (nDstX < 0 || nDstX >= (int) pSurface->nWidth)
            {
                continue;
            }
            s_Under[y * CURSOR_MAX_W + x] = pRow[nDstX];

            const int nSrcX = (int) (x / nScale) - 1;
            if (CursorInk (nSrcX, nSrcY))
            {
                pRow[nDstX] = nBlack;
                continue;
            }
            // The surround: any cell of the margin touching the shape.
            bool bEdge = false;
            for (int dy = -1; dy <= 1 && !bEdge; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    if (CursorInk (nSrcX + dx, nSrcY + dy))
                    {
                        bEdge = true;
                        break;
                    }
                }
            }
            if (bEdge)
            {
                pRow[nDstX] = nWhite;
            }
        }
    }
    s_bCursorShown = true;
    return RectUnion (Was, Box);
}

/*
 *  Text over several lines
 *
 *  Measuring and drawing walk the same loop, with the surface left out of one
 *  of them: two loops that must agree about where a line breaks is two loops
 *  that will one day disagree, and the symptom is an alert whose last line is
 *  drawn outside the box that was reserved for it.
 */
static unsigned TextWrap (TSurface *pSurface, const TOkapiaFont *pFont, const TRect &rBox,
                          const char *pText, TOkapiaColor Color, unsigned nLineHeight)
{
    if (pText == 0 || rBox.nWidth == 0)
    {
        return 0;
    }

    const unsigned char *pLine  = (const unsigned char *) pText;
    const unsigned char *p      = pLine;
    const unsigned char *pBreak = 0;            // after the last space seen
    unsigned nWidth = 0;
    unsigned nLines = 0;
    int      nY     = rBox.nY;

    for (;;)
    {
        const unsigned char *pBefore = p;
        const int nCode = NextChar (&p);
        if (nCode < 0)
        {
            break;
        }
        const int nIndex = GlyphIndex (pFont, nCode);
        const unsigned nAdvance = nIndex < 0 ? 0 : pFont->pWidth[nIndex];

        if (nCode == ' ')
        {
            pBreak = p;
            nWidth += nAdvance;
            continue;
        }

        if (nWidth + nAdvance > rBox.nWidth && pBefore != pLine)
        {
            // Back to the last space if there was one on this line; otherwise
            // break where it stands, which is a word too long for the box.
            const unsigned char *pEnd = pBreak != 0 ? pBreak : pBefore;
            if (pSurface != 0)
            {
                TextRun (pSurface, pFont, rBox.nX,
                         nY + GfxTextTop (pFont, Rect (0, 0, 0, nLineHeight)),
                         pLine, pEnd, Color);
            }
            nLines++;
            nY += (int) nLineHeight;
            pLine  = pEnd;
            p      = pEnd;
            pBreak = 0;
            nWidth = 0;
            continue;
        }
        nWidth += nAdvance;
    }

    if (p != pLine)
    {
        if (pSurface != 0)
        {
            TextRun (pSurface, pFont, rBox.nX,
                     nY + GfxTextTop (pFont, Rect (0, 0, 0, nLineHeight)),
                     pLine, p, Color);
        }
        nLines++;
    }
    return nLines * nLineHeight;
}

unsigned GfxTextWrap (TSurface *pSurface, const TOkapiaFont *pFont, const TRect &rBox,
                      const char *pText, TOkapiaColor Color, unsigned nLineHeight)
{
    // Measured first, then set in the middle of the box. A paragraph drawn from
    // the top of a band that is taller than it — an alert's, where the caution
    // mark sets the height — hangs from the ceiling with all the air below it,
    // which reads as a mistake rather than as a message.
    const unsigned nHigh = TextWrap (0, pFont, rBox, pText, Color, nLineHeight);
    TRect Box = rBox;
    if (nHigh < rBox.nHeight)
    {
        Box.nY += (int) (rBox.nHeight - nHigh) / 2;
    }
    Box.nHeight = nHigh;
    return TextWrap (pSurface, pFont, Box, pText, Color, nLineHeight);
}

unsigned GfxTextWrapHeight (const TOkapiaFont *pFont, unsigned nWidth, const char *pText,
                            unsigned nLineHeight)
{
    return TextWrap (0, pFont, Rect (0, 0, nWidth, 0), pText, ColorBlack, nLineHeight);
}
