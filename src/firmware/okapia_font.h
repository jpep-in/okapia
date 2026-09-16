/*
 * okapia_font.h — the firmware's bitmap font format.
 *
 * Deliberately not Circle's TFont, which is fixed pitch and offers no way to
 * measure a string. The theme needs both a per-glyph advance and measurement:
 * without the first nothing proportional can be drawn, without the second a
 * dialogue cannot be laid out from its own text — which is exactly how
 * localised Macintosh dialogues used to come apart.
 *
 * The tables are generated from the X11 Adobe Helvetica faces vendored in
 * assets/fonts/ (scripts/gen-font.py). Geneva was in substance a Helvetica
 * fitted to a small pixel grid, so this is the nearest thing to it that anyone
 * may redistribute — and being BDF, it needs no rasteriser at build time.
 *
 * Several sizes are compiled in, as a ladder. That is what lets the interface
 * be drawn at the display's own resolution instead of being magnified: a face
 * is chosen for the size actually wanted, so nothing is ever blown up.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_font_h
#define _okapia_font_h

// A glyph occupies nHeight * nBytesPerRow bytes, most significant bit at the
// left, every glyph placed against the common baseline at nAscent. Codes run
// from nFirst to nLast; a handful beyond Latin-1 follow, listed in pExtraCode.
struct TOkapiaFont
{
    unsigned nHeight;
    unsigned nAscent;
    unsigned nCapHeight;                // measured on the H; what text centres on
    unsigned nFirst;
    unsigned nLast;
    unsigned nBytesPerRow;
    const unsigned char  *pBits;
    const unsigned char  *pWidth;       // advance per glyph, same order
    unsigned nExtra;
    const unsigned short *pExtraCode;   // code points beyond nLast, in order
};

// One rung of the ladder: the two weights at a given cell height.
struct TOkapiaFontSet
{
    unsigned nHeight;
    const TOkapiaFont *pRegular;
    const TOkapiaFont *pBold;
};

extern const TOkapiaFontSet OkapiaFaces[];
extern const unsigned OkapiaFaceCount;

// The rung whose cell is nearest the wanted height. Never fails: with one face
// compiled in, that face is the answer.
const TOkapiaFontSet *FontNearest (unsigned nHeight);

#endif
