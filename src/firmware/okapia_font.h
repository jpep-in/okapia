/*
 * okapia_font.h — the firmware's fonts: outline faces rasterised at the height
 * the theme asks for.
 *
 * Deliberately not Circle's TFont, which is fixed pitch and offers no way to
 * measure a string. The theme needs both a per-glyph advance and measurement:
 * without the first nothing proportional can be drawn, without the second a
 * dialogue cannot be laid out from its own text — which is exactly how
 * localised Macintosh dialogues used to come apart.
 *
 * The faces are TeX Gyre Heros, regular and bold (assets/fonts/, GUST Font
 * License), a Helvetica of its own: Geneva was in substance a Helvetica fitted
 * to a small pixel grid. stb_truetype draws them (okapia_font_vector.cpp), once
 * per height, with one byte of coverage a pixel: at any scale the text is the
 * size asked for and its edges are grey, where the X11 bitmaps this replaced
 * came in six sizes with stairs (docs/topics/boot-menu.md, Fonts).
 *
 * Advances are in sixteenths of a pixel and pairs are kerned, so a line is as
 * wide as the face says and "To" sits as the designer set it.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_font_h
#define _okapia_font_h

// One glyph's ink: nWidth x nHeight bytes of coverage at pAlpha + nOffset, its
// left column nLeft pixels right of the pen, its top row nTop rows below the
// top of the line's cell.
struct TOkapiaGlyph
{
    unsigned       nOffset;
    unsigned short nAdvance16;          // to the next pen position, 1/16 px
    unsigned char  nWidth, nHeight;
    unsigned char  nTop;
    signed char    nLeft;
};

// A pair of glyphs set closer or apart, in the face's own units: the same table
// serves every height (fKern16 scales it).
struct TOkapiaKern
{
    unsigned short nLeft, nRight;       // glyph indices (FontGlyphIndex)
    short          nUnits;
};

struct TOkapiaFont
{
    unsigned nHeight;                   // the cell, accented capitals included
    unsigned nAscent;                   // baseline below the cell's top
    unsigned nCapHeight;                // measured on the H; what text centres on
    const TOkapiaGlyph  *pGlyphs;       // one per FontGlyphIndex
    const unsigned char *pAlpha;
    const TOkapiaKern   *pKern;         // sorted by (nLeft, nRight)
    unsigned nKern;
    float    fKern16;                   // face units to 1/16 px
};

struct TOkapiaFontSet
{
    unsigned nHeight;
    const TOkapiaFont *pRegular;
    const TOkapiaFont *pBold;
};

// The characters every face carries, by index: Latin-1 and Latin Extended-A,
// typographic punctuation, a few symbols. -1 when the face has none.
int      FontGlyphIndex (int nCode);
unsigned FontGlyphCount (void);

// How far the pair (nLeft, nRight) moves the pen, 1/16 px; 0 for most.
int FontKern16 (const TOkapiaFont *pFont, int nLeft, int nRight);

// The faces whose cell is nHeight pixels, built on first use and kept: the
// board asks for the same heights at every restart, so it allocates once.
// Never fails; with no memory at all, a face with no ink.
const TOkapiaFontSet *FontNearest (unsigned nHeight);

// The bold face whose capitals are at most nCap pixels tall: a mark drawn as a
// letter (the "i" of a note, the "!" of a caution) is sized by its capital.
const TOkapiaFont *FontBoldWithCap (unsigned nCap);

#endif
