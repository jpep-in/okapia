/*
 * okapia_widgets.h — components: geometry, state, hit testing. Never pixels.
 *
 * A component is a small piece of data, not a record of twenty fields, and a
 * screen is a static array of them with an event loop — the lesson of the
 * Dialog Manager and its item list, stripped of what was only an artefact of
 * 68k code resources. No dispatch on a message number, no loading on demand.
 *
 * The one rule that matters here: a component never draws. It asks the theme.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_widgets_h
#define _okapia_widgets_h

#include "okapia_theme.h"

enum TWidgetType
{
    WidgetLabel,
    WidgetTitle,
    WidgetButton,
    WidgetIconButton,
    WidgetCheckbox,
    WidgetRadio,
    WidgetListFrame,
    WidgetListRow,
    WidgetScrollbar,
    WidgetPopup,
    WidgetField,
    WidgetProgress,
    WidgetSeparator
};

struct TWidget
{
    TWidgetType         Type;
    TRect               Rect;
    const char         *pText;          // label, or the field's contents
    unsigned            nState;         // TPartState bits
    unsigned            nValue;         // scroller position, progress in per mille
    unsigned            nSpan;          // scroller range
    const TGlyphImage  *pIcon;          // a list row's picture, which is real pixel art
    TIconPainter        Paint;          // an icon button's mark, which is computed
    // Which radio buttons are alternatives to one another. Picking one clears
    // the others carrying the same number, and nothing else. A screen with a
    // single group never has to say so; two groups on one screen would have
    // ended up sharing a selection, which is the bug this field exists to make
    // impossible rather than the feature it looks like.
    unsigned            nGroup;
};

// Whether the focus can rest on it: something the user operates, and enabled.
// A list frame answers yes and its rows answer no — the list takes the focus as
// one thing and the arrow keys move the selection inside it, which is how a
// list has always behaved and what stops Tab walking through forty volumes.
bool WidgetFocusable (const TWidget *pWidget);

void WidgetDraw (TSurface *pSurface, const TTheme *pTheme, const TWidget *pWidget);
void WidgetDrawAll (TSurface *pSurface, const TTheme *pTheme, const TWidget *pList,
                    unsigned nCount);

// Index of the topmost component containing the point, or -1. Disabled ones and
// the purely decorative types never answer.
int WidgetHit (const TWidget *pList, unsigned nCount, int nX, int nY);

// Width a button needs for its label, from the theme's own padding — so that a
// screen can lay itself out from its text instead of carrying fixed rectangles,
// which is exactly what used to break translated dialogues.
unsigned WidgetButtonWidth (const TTheme *pTheme, const char *pText, unsigned nState);

#endif
