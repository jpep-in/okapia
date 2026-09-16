/*
 * okapia_layout.h — where a control goes, without a single coordinate.
 *
 * A screen used to be written as a running ordinate: y this, y plus that, and
 * every metric that changed afterwards moved everything below it by hand. Two
 * whole rounds of "the content is in the border" came out of that, and both
 * times only on the right and the bottom, because a layout counted down from
 * the top and nothing ever counted up from the other side.
 *
 * So there are no coordinates here. There is a rectangle being spent: a band is
 * taken off one of its edges and what remains is what is left to place in. The
 * bottom of a dialogue is claimed before its middle is filled, a control that
 * wears a ring reserves what the ring draws outside it, and a layout that runs
 * out of room says so instead of drawing into the frame.
 *
 * It is NSDivideRect and the Mac's own rectangle arithmetic, and nothing more:
 * no constraints, no solver, no second pass.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_layout_h
#define _okapia_layout_h

#include "okapia_theme.h"

struct TLayout
{
    const TTheme *pTheme;
    TRect         Free;                 // what has not been spent yet
    bool          bOverflow;            // a band was asked for that did not fit
};

void LayoutBegin (TLayout *pLayout, const TTheme *pTheme, const TRect &rBounds);

// Bands off the top and the bottom. Asking for more than is left answers the
// remainder and raises bOverflow — never a rectangle outside the bounds, so a
// screen that asks for too much is wrong in its own measurements and not in
// the pixels of the frame it would have drawn over.
TRect LayoutTop (TLayout *pLayout, unsigned nHeight);
TRect LayoutBottom (TLayout *pLayout, unsigned nHeight);

// A band for controls in this state, with what their rings draw outside
// themselves kept clear above and below. Two rows an ordinary gap apart still
// collide when both wear one — found by measuring, never by looking.
//
// **A control that can take the focus is placed with StateFocused**, whatever
// state it is drawn in. The layout runs before the loop and cannot know which
// control will end up focused; a control that can be will be, so the room is
// not optional. Leaving it out put a footer icon's ring one pixel into the
// margin the moment the focus landed on it, and nothing in the layout was
// wrong — the reservation simply had not been asked for.
TRect LayoutRow (TLayout *pLayout, unsigned nHeight, unsigned nState);

void LayoutSkip (TLayout *pLayout, unsigned nAmount);
void LayoutRowGap (TLayout *pLayout);           // between rows of one group
void LayoutSectionGap (TLayout *pLayout);       // between groups

// The same, off the bottom. A footer is built upwards, so the air above its
// rule has to be taken from the bottom too — taken from the top it lands
// nowhere near the rule and the rule sits hard against the buttons, which is
// exactly how it read.
// The mirror of LayoutRow: a band off the bottom with the room its contents
// wear reserved around it. LayoutBottom is the raw one and reserves nothing, so
// a control placed straight into it puts its focus ring into whatever sits
// below — which is how two rows of pop-ups came to have their rings touching.
TRect LayoutRowBottom (TLayout *pLayout, unsigned nHeight, unsigned nState);

void LayoutSkipBottom (TLayout *pLayout, unsigned nAmount);
void LayoutRowGapBottom (TLayout *pLayout);
void LayoutSectionGapBottom (TLayout *pLayout);

unsigned LayoutRoom (const TLayout *pLayout);   // what is still unspent, vertically

// A whole band being spent left to right. The same model turned on its side.
struct TRow
{
    const TTheme *pTheme;
    TRect         Free;
    bool          bFirst;               // no gutter before the first
    // The same, going the other way. A band is filled from both ends — buttons
    // from the right, everything else from the left — and each end owes a
    // gutter between its own neighbours: without this the footer's two buttons
    // were separated by nothing but what they wear, and a default one beside a
    // focused one had its rings almost touching.
    bool          bLast;
    bool          bOverflow;
};

void  RowBegin (TRow *pRow, const TTheme *pTheme, const TRect &rBand);
TRect RowNext (TRow *pRow, unsigned nWidth, unsigned nState);   // from the left
TRect RowLast (TRow *pRow, unsigned nWidth, unsigned nState);   // from the right
TRect RowRest (TRow *pRow, unsigned nState);                    // everything left

// Butts against what precedes it, with no gutter at all: a scroller belongs to
// its list rather than beside it, and a gutter there reads as two controls.
TRect RowAbut (TRow *pRow, unsigned nWidth);
void  RowSkip (TRow *pRow, unsigned nAmount);

// The nIndex-th of nCount columns, with a real gutter between them. Rectangles
// that exactly tile a width share an edge, and each one's focus ring then
// reaches into its neighbour — which is invisible until the neighbour is
// focused too.
TRect RectColumn (const TRect &rRect, unsigned nIndex, unsigned nCount, unsigned nGutter);

#endif
