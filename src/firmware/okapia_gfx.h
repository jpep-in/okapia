/*
 * okapia_gfx.h — surface and drawing primitives for the firmware.
 *
 * These take a TSurface and nothing else. No Circle type appears here, which is
 * the point: the same code draws into the Pi's frame buffer and into a plain
 * buffer on a development machine, so every component can be rendered to a file
 * and looked at without booting anything (tests/host/).
 *
 * The surface is the output's own 32-bit frame buffer, and the interface is
 * drawn straight into it at the display's resolution — no logical canvas, no
 * magnification. That is what keeps a curve smooth: a corner of radius 6 asked
 * for at twice the scale is drawn with a radius of 13 and thirteen pixels of
 * arc, where magnifying a radius-6 corner only enlarges its staircase.
 *
 * What is here is what QuickDraw had and Circle's C2DGraphics has not: a filled
 * and framed rounded rectangle, video inversion, and text with measurement.
 * Regions are absent on purpose — the firmware never overlaps anything.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_gfx_h
#define _okapia_gfx_h

#include "okapia_font.h"

// Roles, not values. What "grey" is worth lives in one table in the
// implementation; nothing else may name a colour.
enum TOkapiaColor
{
    ColorBlack   = 0,
    ColorWhite   = 1,
    ColorGray    = 2,           // the desktop behind the dialogue, and focus
    ColorLtGray  = 3,           // separators and the scroller track
    ColorDim     = 4,           // disabled ink
    ColorDark    = 5,           // filled meters, where black would be too heavy
    ColorCount
};

// A 32-bit surface, 0x00RRGGBB per pixel. On the Pi this is the frame buffer
// itself; in tests/host it is a plain allocation, which is what lets every
// component be rendered to a file without a kernel.
struct TSurface
{
    unsigned char *pPixels;
    unsigned       nWidth;
    unsigned       nHeight;
    unsigned       nPitch;              // bytes per row
};

// Inclusive origin, exclusive extent — x from nX to nX+nWidth-1.
struct TRect
{
    int      nX;
    int      nY;
    unsigned nWidth;
    unsigned nHeight;
};

inline TRect Rect (int nX, int nY, unsigned nWidth, unsigned nHeight)
{
    TRect r = { nX, nY, nWidth, nHeight };
    return r;
}

TRect RectInset (const TRect &rRect, int nDX, int nDY);
bool  RectContains (const TRect &rRect, int nX, int nY);

void GfxClear (TSurface *pSurface, TOkapiaColor Color);
void GfxFill (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color);
void GfxFrame (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color, unsigned nThickness);
void GfxHLine (TSurface *pSurface, int nX, int nY, unsigned nLength, TOkapiaColor Color);
void GfxVLine (TSurface *pSurface, int nX, int nY, unsigned nLength, TOkapiaColor Color);

// Rounded shapes, computed from a distance field and antialiased against what
// is already on the surface. One formula serves the fill and the outline, which
// is what keeps them on the same pixels; and being computed rather than stored,
// they are as smooth at any scale as at 1:1.
void GfxRoundFill (TSurface *pSurface, const TRect &rRect, unsigned nRadius, TOkapiaColor Color);
void GfxRoundFrame (TSurface *pSurface, const TRect &rRect, unsigned nRadius,
                    TOkapiaColor Color, unsigned nThickness);
void GfxCircleFill (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color);
void GfxCircleFrame (TSurface *pSurface, const TRect &rRect, TOkapiaColor Color,
                     unsigned nThickness);

// Swaps black and white, leaves the greys alone. This is the selection
// highlight, and it is a primitive because C2DGraphics has no transfer mode.
void GfxInvert (TSurface *pSurface, const TRect &rRect);

// No synthetic styles. Bold was smeared and italic sheared while the face was
// fixed pitch and single weight; the X11 family carries a real bold, and a real
// oblique (helvO) if one is ever wanted. Faking either now would be worse than
// the thing it imitates, and the theme picks the face anyway.
unsigned GfxTextWidth (const TOkapiaFont *pFont, const char *pText);

// The width of the first nBytes of it. A caret is an offset into the bytes, and
// this is what turns that offset into a place on the screen.
unsigned GfxTextWidthUpTo (const TOkapiaFont *pFont, const char *pText, unsigned nBytes);
void     GfxText (TSurface *pSurface, const TOkapiaFont *pFont, int nX, int nY,
                  const char *pText, TOkapiaColor Color);

// The y a line of text must start at to sit optically centred in rRect. Every
// vertically centred label goes through this, so "centred" means one thing.
int GfxTextTop (const TOkapiaFont *pFont, const TRect &rRect);

enum TTextAlign
{
    TextAlignLeft,
    TextAlignCenter,
    TextAlignRight
};

// Draws pText inside rBox, optically centred vertically, and cut with an
// ellipsis when it will not fit. Every label in the chrome goes through here,
// which is what makes "no text leaves its control" true by construction rather
// than by each part remembering to check. Answers the width actually drawn.
//
// A cut label is drawn from the left whatever alignment was asked for: centring
// one leaves a gap on the left and its ellipsis short of the right edge, which
// reads as a mistake twice over.
unsigned GfxTextBox (TSurface *pSurface, const TOkapiaFont *pFont, const TRect &rBox,
                     const char *pText, TOkapiaColor Color, TTextAlign Align);

// Whether it would be drawn whole. A layout asks this when it wants to widen a
// control rather than let its label be cut.
bool GfxTextFits (const TOkapiaFont *pFont, const TRect &rBox, const char *pText);

// The same text broken between words over as many lines as it takes. This is
// what an alert is made of: a sentence is not a label, it cannot be truncated
// without losing the thing it had to say, and it is the one place the interface
// must give the words the room they need instead of the reverse.
//
// A word wider than the box is broken where it runs out rather than left to
// spill — a volume named without a space in it is not a reason to draw outside
// a control.
unsigned GfxTextWrap (TSurface *pSurface, const TOkapiaFont *pFont, const TRect &rBox,
                      const char *pText, TOkapiaColor Color, unsigned nLineHeight);

// What that would take, without drawing it: a layout has to reserve the room
// before it knows what goes in it.
unsigned GfxTextWrapHeight (const TOkapiaFont *pFont, unsigned nWidth, const char *pText,
                            unsigned nLineHeight);

// A small monochrome image: one row per entry, bit (1 << (nWidth-1-x)) set
// means ink. Icons in the chrome are drawn this way rather than blitted from a
// file — there are three of them and they are ours.
struct TGlyphImage
{
    unsigned        nWidth;
    unsigned        nHeight;
    const unsigned *pRows;
};

// nScale magnifies the bitmap by a whole factor. Icons are line art with no
// larger version to fall back on, so unlike the text and the curves they are
// the one thing still enlarged rather than redrawn — worth knowing when a
// display asks for a big interface.
void GfxImage (TSurface *pSurface, const TGlyphImage *pImage, int nX, int nY,
               TOkapiaColor Color, unsigned nScale);

/*
 *  The pointer
 *
 *  Drawn by the firmware, because nothing else on the screen will: the Mac's own
 *  arrow belongs to a QuickDraw that has not started yet. It keeps what it
 *  covered, and puts it back — which is what a pointer has always cost, and the
 *  reason moving one does not repaint a 1920x1080 display.
 *
 *  Hide before redrawing anything underneath, show again after. Painting over a
 *  shown cursor leaves the old pixels in the save-under, and the next move
 *  stamps them back onto the screen.
 */
void GfxCursorShow (TSurface *pSurface, int nX, int nY, unsigned nScale);
void GfxCursorHide (TSurface *pSurface);

// The one place that knows what a role is worth, in 0x00RRGGBB.
unsigned GfxPaletteEntry (TOkapiaColor Color);

#endif
