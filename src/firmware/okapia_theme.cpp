/*
 * okapia_theme.cpp — the Okapia look: flat, black and white, deliberately undated.
 *
 * What carries "Apple" without dating, and it is not much: black and white, the
 * rounded rectangle that has been the shape of an Apple button without
 * interruption since 1984, the ring around the button Return activates, the
 * grammar of the dialogue — wide margins, centred title, one clear action at
 * bottom right — and flatness. Relief is what dates fastest; Platinum's bevels
 * are the signature of 1997, while the absence of relief was the constraint of
 * 1984 and is the fashion of today. Flat is the exact intersection of the two
 * ends of the story.
 *
 * Avoided on purpose: bevels and gradients, striped title bars, Aqua gel, soft
 * shadows. A hard offset shadow stays undated, and costs one filled rectangle.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_theme.h"

/*
 *  Helpers shared by the parts
 */

static inline TOkapiaColor Ink (unsigned nState)
{
    return (nState & StateDisabled) ? ColorDim : ColorBlack;
}

static void RingAround (TSurface *pSurface, const TRect &rRect, unsigned nRadius,
                        unsigned nGap, unsigned nWidth, TOkapiaColor Color)
{
    const int nOut = (int) (nGap + nWidth);
    GfxRoundFrame (pSurface, RectInset (rRect, -nOut, -nOut), nRadius + (unsigned) nOut,
                   Color, nWidth);
}

/*
 *  Parts
 */

static void DrawDesktop (TSurface *pSurface, const TTheme *)
{
    // A flat grey, not a checkerboard. The Macintosh really did stipple — a
    // QuickDraw pattern fill is painted in foreground and background, so it
    // came out black and white whatever the screen's depth — but our own
    // integer magnification turns a one-pixel checkerboard into crawling 3x3
    // blocks on a 1080p output. The value is the stipple's apparent lightness,
    // not sRGB's mid grey; see okapia_gfx.cpp.
    GfxClear (pSurface, ColorGray);
}

static void DrawDialog (TSurface *pSurface, const TRect &rRect, const TTheme *pTheme)
{
    // A rule, a white margin, a second rule — the structure of a Macintosh
    // alert. No drop shadow: the pair of rules carries the weight on its own,
    // and a shadow only added a smear no reference actually shows.
    // The inner rule carries the weight and the outer one only closes the box.
    // Two rules of the same thickness read as a mistake; the pair only becomes
    // a frame when one of them leads.
    const unsigned nBorder = pTheme->M.nDialogBorder;
    const int nInset = (int) (nBorder + pTheme->M.nDialogGap);
    GfxFill (pSurface, rRect, ColorWhite);
    GfxFrame (pSurface, rRect, ColorBlack, nBorder);
    GfxFrame (pSurface, RectInset (rRect, nInset, nInset), ColorBlack,
              pTheme->M.nDialogInner);
}

// The same frame with the outer rule doubled. An alert has to read as more
// urgent than a dialogue without becoming a different thing: what interrupts is
// heavier, and that is the whole of it.
static void DrawAlert (TSurface *pSurface, const TRect &rRect, const TTheme *pTheme)
{
    const unsigned nBorder = 2 * pTheme->M.nDialogBorder;
    const int nInset = (int) (nBorder + pTheme->M.nDialogGap);
    GfxFill (pSurface, rRect, ColorWhite);
    GfxFrame (pSurface, rRect, ColorBlack, nBorder);
    GfxFrame (pSurface, RectInset (rRect, nInset, nInset), ColorBlack,
              pTheme->M.nDialogInner);
}

static void DrawTitle (TSurface *pSurface, const TRect &rRect, const char *pText,
                       const TTheme *pTheme)
{
    GfxTextBox (pSurface, pTheme->pTitleBoldFont, rRect, pText, ColorBlack, TextAlignCenter);
}

static void DrawLabel (TSurface *pSurface, const TRect &rRect, const char *pText,
                       unsigned nState, const TTheme *pTheme)
{
    // A real bold face, not a smeared one: the weight is the designer's.
    const TOkapiaFont *pFace = (nState & StateStrong) ? pTheme->pBodyBoldFont
                                                      : pTheme->pBodyFont;
    GfxTextBox (pSurface, pFace, rRect, pText, Ink (nState), TextAlignLeft);
}

static void DrawParagraph (TSurface *pSurface, const TRect &rRect, const char *pText,
                           unsigned nState, const TTheme *pTheme)
{
    const TOkapiaFont *pFace = (nState & StateStrong) ? pTheme->pBodyBoldFont
                                                      : pTheme->pBodyFont;
    GfxTextWrap (pSurface, pFace, rRect, pText, Ink (nState), pTheme->M.nLineHeight);
}

// Grey and one pixel: the focus has to be findable, not shouted. In black it
// competed with the default button's ring, and the two together read as a
// mistake rather than as two different things.
static void DrawFocusRing (TSurface *pSurface, const TRect &rRect, unsigned nRadius,
                           const TTheme *pTheme)
{
    RingAround (pSurface, rRect, nRadius, pTheme->M.nFocusGap, pTheme->M.nStroke, ColorGray);
}

static void DrawButton (TSurface *pSurface, const TRect &rRect, const char *pText,
                        unsigned nState, const TTheme *pTheme)
{
    const unsigned nRadius = pTheme->M.nButtonRadius;
    const bool bPressed = (nState & StatePressed) != 0;

    // Painted rather than inverted. Inverting a rectangle over a rounded shape
    // turns the four corners inside out, which shows as white notches — the
    // rule for selection belongs to rectangles, and a button is not one.
    GfxRoundFill (pSurface, rRect, nRadius, bPressed ? ColorBlack : ColorWhite);
    GfxRoundFrame (pSurface, rRect, nRadius, Ink (nState), pTheme->M.nStroke);
    // Inset by the padding the button was measured with, so that a label too
    // long for its button is cut inside the padding rather than against the
    // outline — which is what a translated dialogue does to a fixed width.
    GfxTextBox (pSurface,
                (nState & StateDefault) ? pTheme->pBodyBoldFont : pTheme->pBodyFont,
                RectInset (rRect, (int) pTheme->M.nButtonPadX, 0),
                pText, bPressed ? ColorWhite : Ink (nState), TextAlignCenter);

    if (nState & StateDefault)
    {
        RingAround (pSurface, rRect, nRadius, pTheme->M.nRingGap, pTheme->M.nRingWidth,
                    ColorBlack);
    }
    if (nState & StateFocused)
    {
        DrawFocusRing (pSurface, rRect, nRadius, pTheme);
    }
}

static void DrawIconButton (TSurface *pSurface, const TRect &rRect, TIconPainter Paint,
                            unsigned nState, const TTheme *pTheme)
{
    const unsigned nRadius = pTheme->M.nButtonRadius;
    const bool bPressed = (nState & StatePressed) != 0;
    const TOkapiaColor Back = bPressed ? ColorBlack : ColorWhite;

    GfxRoundFill (pSurface, rRect, nRadius, Back);
    GfxRoundFrame (pSurface, rRect, nRadius, Ink (nState), pTheme->M.nStroke);

    if (Paint != 0)
    {
        const unsigned nSide = (rRect.nWidth < rRect.nHeight ? rRect.nWidth : rRect.nHeight)
                             * 2 / 3;
        Paint (pSurface, Rect (rRect.nX + (int) (rRect.nWidth  - nSide) / 2,
                               rRect.nY + (int) (rRect.nHeight - nSide) / 2, nSide, nSide),
               bPressed ? ColorWhite : Ink (nState), Back);
    }

    if (nState & StateFocused)
    {
        DrawFocusRing (pSurface, rRect, nRadius, pTheme);
    }
}

static void DrawIcon (TSurface *pSurface, const TRect &rRect, TIconPainter Paint,
                      unsigned nState, const TTheme *pTheme)
{
    (void) pTheme;
    if (Paint != 0)
    {
        Paint (pSurface, rRect, Ink (nState), ColorWhite);
    }
}

// A tick, kept strictly inside the box and centred in it. Both strokes are laid
// out from the box, then the whole mark is shifted so that its own extent — not
// the layout box it was built in — sits in the middle. A tick is taller on one
// side than the other, so building it in a centred box leaves it visibly high.
static void Tick (TSurface *pSurface, const TRect &rBox, TOkapiaColor Color)
{
    const int N = (int) rBox.nWidth;
    const int m = N / 5 < 2 ? 2 : N / 5;
    const int t = N / 7 < 1 ? 1 : N / 7;
    const int w = N - 2 * m;
    const int h = N - 2 * m;
    const int nDown = w * 2 / 5 < 1 ? 1 : w * 2 / 5;
    const int nUp = w - nDown;

    // The mark's own extent, and the origin that centres that extent — not the
    // layout box it was built in. Laying the two strokes out in a box that is
    // itself centred leaves the tick visibly high, because a tick is far taller
    // on its long side than on its short one.
    const int nInkTop = h - t - (nUp - 1);
    const int nInkH   = t + nUp - 1;

    const int x0 = rBox.nX + m;
    // Rounded so the spare half-row falls above the mark rather than below it:
    // an odd remainder has to go somewhere, and a tick sitting low reads as
    // centred where a tick sitting high reads as loose.
    const int y0 = rBox.nY + (N - nInkH + 1) / 2 - nInkTop;

    for (int i = 0; i < nDown; i++)
    {
        GfxFill (pSurface, Rect (x0 + i, y0 + h - nDown + i - t, 1, (unsigned) t), Color);
    }
    for (int i = 0; i < nUp; i++)
    {
        GfxFill (pSurface, Rect (x0 + nDown + i, y0 + h - t - i, 1, (unsigned) t), Color);
    }
}

// What is left of a control once its box or its circle has been taken out. The
// obvious version passes the control's whole width for the label and lets it run
// that much past the right edge — invisible until two of them stand side by side.
static TRect LabelBox (const TRect &rRect, unsigned nSize, const TTheme *pTheme)
{
    const int nInk = (int) nSize + (int) pTheme->M.nGap;
    return Rect (rRect.nX + nInk, rRect.nY,
                 (unsigned) ((int) rRect.nWidth - nInk), rRect.nHeight);
}

static void DrawCheckbox (TSurface *pSurface, const TRect &rRect, const char *pText,
                          unsigned nState, const TTheme *pTheme)
{
    const unsigned nSize = pTheme->M.nCheckSize;
    const TRect Box = Rect (rRect.nX, rRect.nY + ((int) rRect.nHeight - (int) nSize) / 2,
                            nSize, nSize);

    GfxFill (pSurface, Box, ColorWhite);
    GfxFrame (pSurface, Box, Ink (nState), pTheme->M.nStroke);
    if (nState & StateChecked)
    {
        Tick (pSurface, Box, Ink (nState));
    }
    if (nState & StatePressed)
    {
        GfxInvert (pSurface, RectInset (Box, 1, 1));
    }

    DrawLabel (pSurface, LabelBox (rRect, nSize, pTheme), pText,
               nState & ~StateSelected, pTheme);

    if (nState & StateFocused)
    {
        DrawFocusRing (pSurface, Box, 2, pTheme);       // a box, not a circle
    }
}

static void DrawRadio (TSurface *pSurface, const TRect &rRect, const char *pText,
                       unsigned nState, const TTheme *pTheme)
{
    const unsigned nSize = pTheme->M.nCheckSize;
    const TRect Box = Rect (rRect.nX, rRect.nY + ((int) rRect.nHeight - (int) nSize) / 2,
                            nSize, nSize);

    GfxCircleFill (pSurface, Box, ColorWhite);
    GfxCircleFrame (pSurface, Box, Ink (nState), pTheme->M.nStroke);
    if (nState & StateChecked)
    {
        // A good third of the circle stays white between the ring and the dot.
        // Filling it nearly to the ring is what made it look like a blob.
        const int nAir = (int) nSize / 3;
        GfxCircleFill (pSurface, RectInset (Box, nAir, nAir), Ink (nState));
    }

    if (pText != 0)
    {
        DrawLabel (pSurface, LabelBox (rRect, nSize, pTheme), pText,
                   nState & ~StateSelected, pTheme);
    }
    if (nState & StateFocused)
    {
        DrawFocusRing (pSurface, Box, nSize / 2, pTheme);
    }
}

static void DrawListFrame (TSurface *pSurface, const TRect &rRect, const TTheme *pTheme)
{
    GfxFill (pSurface, rRect, ColorWhite);
    GfxFrame (pSurface, rRect, ColorBlack, pTheme->M.nStroke);
}

static void DrawListRow (TSurface *pSurface, const TRect &rRect, unsigned, const TTheme *)
{
    GfxFill (pSurface, rRect, ColorWhite);
}

static void DrawSelection (TSurface *pSurface, const TRect &rRect, const TTheme *)
{
    GfxInvert (pSurface, rRect);
}

// No arrow boxes at either end: they are what would place this in 1990, and
// nothing needs them once the keyboard drives the list.
static void DrawScrollbar (TSurface *pSurface, const TRect &rRect, unsigned nTop,
                           unsigned nVisible, unsigned nTotal, const TTheme *pTheme)
{
    GfxFill (pSurface, rRect, ColorLtGray);
    GfxFrame (pSurface, rRect, ColorBlack, pTheme->M.nStroke);

    if (nTotal == 0 || nVisible == 0 || nVisible >= nTotal)
    {
        return;                         // everything is in view: no thumb to draw
    }
    const unsigned nTrack = rRect.nHeight - 2;
    const unsigned nMin = rRect.nWidth * 2;      // never a sliver: two squares

    // As much of the track as the view is of the whole. That is what tells the
    // reader how much is out of sight, and it is the only thing a scroller has
    // ever been read for at a glance.
    unsigned nThumb = nTrack * nVisible / nTotal;
    if (nThumb < nMin)
    {
        nThumb = nMin;
    }
    if (nThumb > nTrack)
    {
        nThumb = nTrack;
    }
    const unsigned nTravel = nTrack - nThumb;
    const unsigned nSteps  = nTotal - nVisible;
    const unsigned nAt = nTop > nSteps ? nTravel : nTravel * nTop / nSteps;

    const TRect Thumb = Rect (rRect.nX + 1, rRect.nY + 1 + (int) nAt,
                              rRect.nWidth - 2, nThumb);
    GfxFill (pSurface, Thumb, ColorWhite);
    GfxFrame (pSurface, Thumb, ColorBlack, pTheme->M.nStroke);
}

static void DrawPopup (TSurface *pSurface, const TRect &rRect, const char *pText,
                       unsigned nState, const TTheme *pTheme)
{
    // Its own radius: a pop-up is wide and low where a button is short and
    // tall, and the same corner reads far rounder on it.
    const unsigned nRadius = pTheme->M.nPopupRadius;
    GfxRoundFill (pSurface, rRect, nRadius, ColorWhite);
    GfxRoundFrame (pSurface, rRect, nRadius, Ink (nState), pTheme->M.nStroke);

    // The triangle's room is taken out before the label, not left to chance:
    // a long choice used to run straight under the arrow.
    const int nArrow = (int) rRect.nHeight / 3 + (int) pTheme->M.nGap;
    const TRect Text = Rect (rRect.nX + (int) pTheme->M.nGap, rRect.nY,
                             (unsigned) ((int) rRect.nWidth - 2 * (int) pTheme->M.nGap
                                         - nArrow),
                             rRect.nHeight);
    GfxTextBox (pSurface, pTheme->pBodyFont, Text, pText, Ink (nState), TextAlignLeft);

    // A solid triangle pointing down: the shape that has said "there is a list
    // behind this" in every decade. Sized from the control, not from a
    // constant, for the same reason as the tick.
    const int nHalf = (int) rRect.nHeight / 6 < 2 ? 2 : (int) rRect.nHeight / 6;
    const int nCX = rRect.nX + (int) rRect.nWidth - (int) pTheme->M.nGap - nHalf;
    const int nCY = rRect.nY + ((int) rRect.nHeight - (nHalf + 1)) / 2;
    for (int row = 0; row <= nHalf; row++)
    {
        GfxHLine (pSurface, nCX - nHalf + row, nCY + row,
                  (unsigned) (2 * (nHalf - row) + 1), Ink (nState));
    }

    if (nState & StateFocused)
    {
        DrawFocusRing (pSurface, rRect, pTheme->M.nPopupRadius, pTheme);
    }
}

static void DrawField (TSurface *pSurface, const TRect &rRect, const char *pText,
                       unsigned nState, unsigned nCaret, const TTheme *pTheme)
{
    GfxFill (pSurface, rRect, ColorWhite);
    GfxFrame (pSurface, rRect, Ink (nState), pTheme->M.nStroke);

    const int nPad = (int) pTheme->M.nGap / 2;
    const TRect Text = Rect (rRect.nX + nPad, rRect.nY,
                             (unsigned) ((int) rRect.nWidth - 2 * nPad), rRect.nHeight);
    const int nY = GfxTextTop (pTheme->pBodyFont, rRect);
    const unsigned nWidth = GfxTextBox (pSurface, pTheme->pBodyFont, Text, pText,
                                        Ink (nState), TextAlignLeft);

    if (nState & StateFocused)
    {
        // At the insertion point, and never past the text actually drawn: a cut
        // label ends in an ellipsis, and a caret standing beyond it would be
        // pointing at something nobody can see.
        unsigned nAt = GfxTextWidthUpTo (pTheme->pBodyFont, pText, nCaret);
        if (nAt > nWidth)
        {
            nAt = nWidth;
        }
        GfxFill (pSurface, Rect (Text.nX + (int) nAt, nY, pTheme->M.nStroke,
                                 pTheme->pBodyFont->nHeight), ColorBlack);
        DrawFocusRing (pSurface, rRect, 0, pTheme);
    }
}

static void DrawProgress (TSurface *pSurface, const TRect &rRect, unsigned nPerMille,
                          const TTheme *pTheme)
{
    GfxFill (pSurface, rRect, ColorWhite);
    GfxFrame (pSurface, rRect, ColorBlack, pTheme->M.nStroke);

    if (nPerMille > 1000)
    {
        nPerMille = 1000;
    }
    const unsigned nInner = rRect.nWidth > 4 ? rRect.nWidth - 4 : 0;
    const unsigned nDone = nInner * nPerMille / 1000;
    if (nDone != 0)
    {
        // Dark grey rather than black: a full black bar is heavier than
        // anything else on the screen and drags the eye off the text.
        GfxFill (pSurface, Rect (rRect.nX + 2, rRect.nY + 2, nDone, rRect.nHeight - 4),
                 ColorDark);
    }
}

static void DrawSeparator (TSurface *pSurface, const TRect &rRect, const TTheme *)
{
    GfxHLine (pSurface, rRect.nX, rRect.nY, rRect.nWidth, ColorLtGray);
}

/*
 *  The theme itself
 */

static const TThemeMetrics s_Base =
{
    1,      // nStroke
    14,     // nMargin
    8,      // nGap
    4,      // nRowGap — the rhythm as it stood, and it read well
    8,      // nSectionGap
    16,     // nLineHeight
    34,     // nRowHeight — tall enough for a system folder icon
    22,     // nButtonHeight
    14,     // nButtonPadX
    6,      // nButtonRadius
    3,      // nPopupRadius
    64,     // nButtonMinWidth
    2,      // nRingGap
    3,      // nRingWidth
    3,      // nFocusGap
    14,     // nScrollbarWidth
    12,     // nCheckSize
    1,      // nDialogBorder
    4,      // nDialogGap
    2,      // nDialogInner
    20,     // nFieldHeight
    12,     // nProgressHeight
    14,     // nBodyHeight
    16      // nTitleHeight
};

const TTheme OkapiaThemeBase =
{
    16, 1, s_Base,
    0, 0, 0, 0,                         // faces are chosen by ThemeMake

    DrawDesktop,
    DrawDialog,
    DrawAlert,
    DrawTitle,
    DrawLabel,
    DrawParagraph,
    DrawButton,
    DrawIconButton,
    DrawIcon,
    DrawCheckbox,
    DrawRadio,
    DrawListFrame,
    DrawListRow,
    DrawSelection,
    DrawScrollbar,
    DrawPopup,
    DrawField,
    DrawProgress,
    DrawSeparator,
    DrawFocusRing
};

/*
 *  Scaling
 */

TRect ThemeContent (const TRect &rDialog, const TTheme *pTheme)
{
    const int nFrame = (int) (pTheme->M.nDialogBorder + pTheme->M.nDialogGap
                              + pTheme->M.nDialogInner);
    return RectInset (rDialog, nFrame + (int) pTheme->M.nMargin,
                      nFrame + (int) pTheme->M.nMargin);
}

unsigned ThemeReach (const TTheme *pTheme, unsigned nState)
{
    unsigned nOut = 0;
    if (nState & StateDefault)
    {
        nOut = pTheme->M.nRingGap + pTheme->M.nRingWidth;
    }
    if (nState & StateFocused)
    {
        const unsigned nFocus = pTheme->M.nFocusGap + pTheme->M.nStroke;
        if (nFocus > nOut)
        {
            nOut = nFocus;
        }
    }
    // Plus the antialiased fringe. A ring is drawn from a distance field, so its
    // ink reaches a pixel past the geometry it was asked for — and a layout that
    // reserves only the geometry lets that pixel into the margin.
    return nOut == 0 ? 0 : nOut + 1;
}

unsigned ThemeScaleFor (unsigned nWidth, unsigned nHeight)
{
    // The base design is 640x480. The factor is the smaller of the two ratios,
    // in sixteenths, so the interface always fits whatever the aspect ratio.
    const unsigned nByWidth  = nWidth  * 16 / 640;
    const unsigned nByHeight = nHeight * 16 / 480;
    unsigned nScale = nByWidth < nByHeight ? nByWidth : nByHeight;

    if (nScale < 16)
    {
        nScale = 16;                    // never smaller than the design
    }
    if (nScale > 64)
    {
        nScale = 64;                    // and four times is already very large
    }
    return nScale;
}

static unsigned Scaled (unsigned nValue, unsigned nScale16, unsigned nFloor)
{
    const unsigned n = nValue * nScale16 / 16;
    return n < nFloor ? nFloor : n;
}

void ThemeMake (unsigned nScale16, TTheme *pOut)
{
    *pOut = OkapiaThemeBase;
    pOut->nScale16 = nScale16;
    // Icons are bitmaps with no larger version, so they take a whole factor
    // while everything else takes the real one.
    pOut->nIconScale = nScale16 / 16;
    if (pOut->nIconScale == 0)
    {
        pOut->nIconScale = 1;
    }

    const TThemeMetrics &B = s_Base;
    TThemeMetrics &M = pOut->M;

    // Every outline in the chrome takes this. Leaving a bare 1 in the drawing
    // code is what made a hairline button sit inside a four-pixel ring at twice
    // the scale — the same trap as the tick, the triangle and the focus ring,
    // and the fourth time it has been sprung here.
    M.nStroke         = Scaled (B.nStroke,         nScale16, 1);
    M.nMargin         = Scaled (B.nMargin,         nScale16, 4);
    M.nGap            = Scaled (B.nGap,            nScale16, 2);
    M.nRowGap         = Scaled (B.nRowGap,         nScale16, 3);
    M.nSectionGap     = Scaled (B.nSectionGap,     nScale16, 6);
    M.nLineHeight     = Scaled (B.nLineHeight,     nScale16, 4);
    M.nRowHeight      = Scaled (B.nRowHeight,      nScale16, 8);
    M.nButtonHeight   = Scaled (B.nButtonHeight,   nScale16, 8);
    M.nButtonPadX     = Scaled (B.nButtonPadX,     nScale16, 4);
    M.nButtonRadius   = Scaled (B.nButtonRadius,   nScale16, 2);
    M.nPopupRadius    = Scaled (B.nPopupRadius,    nScale16, 2);
    M.nButtonMinWidth = Scaled (B.nButtonMinWidth, nScale16, 16);
    M.nRingGap        = Scaled (B.nRingGap,        nScale16, 1);
    M.nRingWidth      = Scaled (B.nRingWidth,      nScale16, 1);
    M.nFocusGap       = Scaled (B.nFocusGap,       nScale16, 1);
    M.nScrollbarWidth = Scaled (B.nScrollbarWidth, nScale16, 6);
    M.nCheckSize      = Scaled (B.nCheckSize,      nScale16, 8);
    M.nDialogBorder   = Scaled (B.nDialogBorder,   nScale16, 1);
    M.nDialogGap      = Scaled (B.nDialogGap,      nScale16, 2);
    M.nDialogInner    = Scaled (B.nDialogInner,    nScale16, 2);
    M.nFieldHeight    = Scaled (B.nFieldHeight,    nScale16, 8);
    M.nProgressHeight = Scaled (B.nProgressHeight, nScale16, 4);
    M.nBodyHeight     = Scaled (B.nBodyHeight,     nScale16, 8);
    M.nTitleHeight    = Scaled (B.nTitleHeight,    nScale16, 8);

    // The faces are picked from the ladder rather than magnified, which is the
    // whole point of compiling several sizes in.
    const TOkapiaFontSet *pBody  = FontNearest (M.nBodyHeight);
    const TOkapiaFontSet *pTitle = FontNearest (M.nTitleHeight);
    pOut->pBodyFont      = pBody->pRegular;
    pOut->pBodyBoldFont  = pBody->pBold;
    pOut->pTitleFont     = pTitle->pRegular;
    pOut->pTitleBoldFont = pTitle->pBold;

    // The controls follow the face that lands, not the size that was wished
    // for: asking for a 28-row body and getting 27 must not leave a button
    // sized for a line it no longer has.
    const unsigned nLine = pOut->pBodyFont->nHeight;
    if (M.nButtonHeight < nLine + 8)
    {
        M.nButtonHeight = nLine + 8;
    }
    if (M.nFieldHeight < nLine + 6)
    {
        M.nFieldHeight = nLine + 6;
    }
    if (M.nLineHeight < nLine + 2)
    {
        M.nLineHeight = nLine + 2;
    }
}
