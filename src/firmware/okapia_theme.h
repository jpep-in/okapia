/*
 * okapia_theme.h — the one seam through which every pixel of the interface passes.
 *
 * A theme only exists if there is exactly one place that draws. One button
 * painted anywhere else and the promise is void — and the promise is worth
 * having: correcting a part corrects it everywhere it appears, from the boot
 * chooser to the repair alert, without having to remember the list.
 *
 * The trap Apple took ten years to fix is that **metrics belong to the theme as
 * much as pixels do**. A component that decides a button is twenty pixels tall
 * makes the theme unreplaceable — no roomier theme, no tighter one — and the
 * first translation breaks the layout. So components ask; they never assume.
 * This is GetThemeMetric standing beside DrawThemeButton, taken as it was.
 *
 * The look is an uchronia, not a reproduction: this menu opens in front of
 * Systems from 6 to 9, so copying any one of them would date it against its own
 * contents. See planification.md §7.12.
 *
 * Metrics being the theme's is also what makes resolution independence possible
 * at all: a scale factor multiplies them, the interface is drawn at the
 * display's own resolution, and nothing is ever magnified. ThemeMake() is where
 * that happens, and it is the only place a scale factor appears.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_theme_h
#define _okapia_theme_h

#include "okapia_icons.h"

// State is a bit set, not an enumeration: a button can be the default one, hold
// the focus and be disabled at the same time, and the theme has to say what
// that looks like.
enum TPartState
{
    StateNormal   = 0,
    StatePressed  = 1u << 0,
    StateDisabled = 1u << 1,
    StateDefault  = 1u << 2,            // Return activates it
    StateFocused  = 1u << 3,
    StateChecked  = 1u << 4,
    StateSelected = 1u << 5,            // a row picked out in a list
    StateStrong   = 1u << 6,            // a label that carries weight, e.g. a heading
    // The caret is showing right now. It blinks, so a focused field is not
    // enough to say whether the mark is on the screen at this instant.
    StateCaret    = 1u << 7,
    // Operated, but never focused. An alert with two answers has no navigation
    // to offer — Return is yes and Escape is no, and both work wherever the
    // hand is — so its buttons take no ring. Without this the default one wore
    // two: the ring that says "Return does this" and the ring that says "the
    // keyboard is here", one inside the other, saying the same thing twice.
    StateNoFocus  = 1u << 8
};

// What a field has to say about itself beyond its text. Only the theme knows
// where the letters were drawn, so it is the theme that turns byte offsets into
// a caret and a highlight — and passing them as one thing keeps the drawing
// call from becoming a row of six numbers nobody reads.
struct TFieldMark
{
    unsigned nCaret;                    // the insertion point, a byte offset
    unsigned nAnchor;                   // where a selection began; equal when none
};

struct TThemeMetrics
{
    unsigned nStroke;                   // the ordinary line weight of the chrome
    unsigned nMargin;                   // dialogue edge to content
    unsigned nGap;                      // between neighbouring controls, horizontally
    unsigned nRowGap;                   // between rows of controls in one group
    unsigned nSectionGap;               // between groups
    unsigned nLineHeight;
    unsigned nRowHeight;                // one row of a list
    // One item of a menu. Shorter than a list's row, which is tall enough for a
    // folder icon: a menu carries words, and a list's rhythm makes it loose.
    unsigned nMenuRow;
    unsigned nButtonHeight;
    unsigned nButtonPadX;               // ink to button edge, horizontally
    unsigned nButtonRadius;
    unsigned nPopupRadius;              // wider and shorter than a button, so less round
    unsigned nButtonMinWidth;
    unsigned nRingGap;                  // default-button ring, gap to the button
    unsigned nRingWidth;
    unsigned nFocusGap;
    unsigned nScrollbarWidth;
    unsigned nCheckSize;
    unsigned nDialogBorder;
    unsigned nDialogGap;                // white between the two rules of the frame
    unsigned nDialogInner;              // the inner rule, heavier than the outer
    unsigned nFieldHeight;
    unsigned nProgressHeight;
    unsigned nBodyHeight;               // wanted cell for the body face
    unsigned nTitleHeight;
};

struct TTheme
{
    unsigned            nScale16;       // 16 is 1:1; fractional scales are allowed
    unsigned            nIconScale;     // whole factor, icons having no larger art
    TThemeMetrics       M;              // already multiplied by the scale
    const TOkapiaFont  *pTitleFont;
    const TOkapiaFont  *pTitleBoldFont;
    const TOkapiaFont  *pBodyFont;
    const TOkapiaFont  *pBodyBoldFont;

    void (*DrawDesktop)      (TSurface *, const TTheme *);
    void (*DrawDialog)       (TSurface *, const TRect &, const TTheme *);
    // An alert is a dialogue that interrupts, and it says so with its caution
    // mark and its words — not with a heavier frame. The two rule thicknesses
    // are a constant of the design and the same everywhere; a frame that is
    // nearly the dialogue's reads as a mistake, not as emphasis.
    void (*DrawAlert)        (TSurface *, const TRect &, const TTheme *);
    void (*DrawTitle)        (TSurface *, const TRect &, const char *, const TTheme *);
    void (*DrawLabel)        (TSurface *, const TRect &, const char *, unsigned, const TTheme *);
    // A sentence, broken between words. Not a label: a label that will not fit
    // is cut, and a sentence that is cut has lost the thing it had to say.
    void (*DrawParagraph)    (TSurface *, const TRect &, const char *, unsigned,
                              const TTheme *);
    void (*DrawButton)       (TSurface *, const TRect &, const char *, unsigned, const TTheme *);
    void (*DrawIconButton)   (TSurface *, const TRect &, TIconPainter, unsigned,
                              const TTheme *);
    // The same mark with nothing under it. An alert's caution sign is not a
    // button and must not look like one: drawn as a disabled icon button it
    // came out grey and inside a frame, offering to be clicked.
    void (*DrawIcon)         (TSurface *, const TRect &, TIconPainter, unsigned,
                              const TTheme *);
    void (*DrawCheckbox)     (TSurface *, const TRect &, const char *, unsigned, const TTheme *);
    void (*DrawRadio)        (TSurface *, const TRect &, const char *, unsigned, const TTheme *);
    void (*DrawListFrame)    (TSurface *, const TRect &, const TTheme *);
    void (*DrawListRow)      (TSurface *, const TRect &, unsigned, const TTheme *);
    // Applied *after* a row's contents, so that whatever the screen drew into
    // it — text, icons, a checkbox — is picked out by the same rule. This is
    // the one place video inversion is right: a row is a rectangle, and a
    // rounded button is not.
    void (*DrawSelection)    (TSurface *, const TRect &, const TTheme *);
    // Three numbers and not two: where the view starts, how much of the whole
    // it shows, and how much there is. A thumb sized from a step count says
    // nothing about how much is out of sight, which is the one thing a scroller
    // is looked at for.
    void (*DrawScrollbar)    (TSurface *, const TRect &, unsigned nTop, unsigned nVisible,
                              unsigned nTotal, const TTheme *);
    void (*DrawPopup)        (TSurface *, const TRect &, const char *, unsigned, const TTheme *);
    void (*DrawField)        (TSurface *, const TRect &, const char *, unsigned,
                              const TFieldMark &, const TTheme *);
    void (*DrawProgress)     (TSurface *, const TRect &, unsigned, const TTheme *);
    void (*DrawSeparator)    (TSurface *, const TRect &, const TTheme *);
    // The radius comes from the caller so that the ring follows the shape it
    // surrounds — a pop-up is not as round as a button, and a tick box is not
    // round at all. Passing the button's radius everywhere is what made the
    // ring sit wrong on both.
    // Rectangle, corner radius, and how far out to sit. The gap is a parameter
    // because the default button puts its ring outside the black one instead
    // of inside it; everything else passes M.nFocusGap.
    void (*DrawFocusRing)    (TSurface *, const TRect &, unsigned, unsigned,
                              const TTheme *);
};

// The theme at 1:1. A second theme would be a second value of this type, and
// nothing in the components would change.
extern const TTheme OkapiaThemeBase;

// How far inside a field its text begins. The theme draws it there and a click
// has to find it there; two copies of that number is a caret landing beside the
// letter it was aimed at.
unsigned ThemeFieldInset (const TTheme *pTheme);

// Where a dialogue's contents may start: inside its frame, then clear of it by
// the margin. Measuring the margin from the dialogue's outer edge instead —
// which is the obvious thing to write and what stood here — gives away the
// frame's own thickness and leaves the contents half as far from it as the
// metric claims. Every screen goes through this, so "margin" means one thing.
TRect ThemeContent (const TRect &rDialog, const TTheme *pTheme);

// How far outside its own rectangle a control in this state will draw. A
// default button wears a ring and a focused one wears another, and a layout
// that places such a control flush against a margin pushes that ring into it.
// Asking is the only way to know: those widths are the theme's, not the screen's.
unsigned ThemeReach (const TTheme *pTheme, unsigned nState);

// Where a scroller's thumb lies inside its track. The theme draws it and the
// event loop has to hit-test it, and two answers to that question means a hand
// landing beside the thumb it can see — so there is one, and it is here.
TRect ThemeScrollThumb (const TTheme *pTheme, const TRect &rBar, unsigned nTop,
                        unsigned nVisible, unsigned nTotal);

// Whole sixteenths: 16 is 1:1, 36 is the 2.25 a 1920x1080 output asks for. The
// base design is 640x480, and the factor is the smaller of the two ratios so
// that the interface always fits.
unsigned ThemeScaleFor (unsigned nWidth, unsigned nHeight);

// Fills pOut with the base theme, its metrics multiplied and its faces chosen
// from the ladder. This is the only place a scale factor is applied.
void ThemeMake (unsigned nScale16, TTheme *pOut);

#endif
