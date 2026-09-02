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
    WidgetParagraph,                    // a sentence, broken between words
    WidgetTitle,
    WidgetAlert,                        // the frame of one, drawn behind its contents
    WidgetButton,
    WidgetIconButton,
    WidgetIcon,                         // a painted mark on its own
    WidgetCheckbox,
    WidgetRadio,
    WidgetList,
    WidgetScrollbar,
    WidgetPopup,
    WidgetField,
    WidgetProgress,
    WidgetSeparator
};

// What a list holds. Rows are not components of their own: a list that scrolls
// cannot be an array of row components, because every scroll would mean
// rebuilding them and the screen above would have to be told when. The Mac's
// List Manager kept its cells for the same reason.
struct TListItem
{
    const char        *pText;
    const TGlyphImage *pIcon;
    unsigned           nState;          // StateDisabled, and later a chosen mark
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

    // A list's contents.
    const TListItem    *pItems;
    unsigned            nItems;
    unsigned            nTop;           // first row shown
    int                 nChoice;        // item picked, or -1

    // A field's own storage. A field without it is read-only, which is what
    // every other control is.
    char               *pEdit;
    unsigned            nEditSize;      // bytes available, terminator included
    unsigned            nCaret;         // the insertion point, as a byte offset
};

// Fills one in with nothing set. Every screen builds its components through
// this: a field left uninitialised in a struct this wide is a pointer nobody
// wrote, and it would be read the first time somebody scrolled.
void WidgetClear (TWidget *pWidget);

void WidgetDraw (TSurface *pSurface, const TTheme *pTheme, const TWidget *pWidget);
void WidgetDrawAll (TSurface *pSurface, const TTheme *pTheme, const TWidget *pList,
                    unsigned nCount);

// Index of the topmost component containing the point, or -1. Disabled ones and
// the purely decorative types never answer.
int WidgetHit (const TWidget *pList, unsigned nCount, int nX, int nY);

// Whether the focus can rest on it: something the user operates, and enabled.
bool WidgetFocusable (const TWidget *pWidget);

// Width a button needs for its label, from the theme's own padding — so that a
// screen can lay itself out from its text instead of carrying fixed rectangles,
// which is exactly what used to break translated dialogues.
unsigned WidgetButtonWidth (const TTheme *pTheme, const char *pText, unsigned nState);

/*
 *  Lists
 *
 *  The geometry of the rows belongs here, because nothing else can work it out:
 *  the rows are not components, so there is no rectangle to hit-test against.
 */
unsigned WidgetListVisible (const TWidget *pWidget, const TTheme *pTheme);
int      WidgetListItemAt (const TWidget *pWidget, const TTheme *pTheme, int nX, int nY);

// The scroller inside the frame, or an empty rectangle when everything fits.
TRect    WidgetListScroller (const TWidget *pWidget, const TTheme *pTheme);

// Brings the chosen item into view, scrolling by as little as it takes. A list
// that jumps to put the selection in the middle loses the reader's place.
void     WidgetListReveal (TWidget *pWidget, const TTheme *pTheme);

/*
 *  Editable fields
 *
 *  A field owns its bytes and its insertion point; the screen owns when they
 *  change. Everything here is a no-op on a field with no storage, so a
 *  read-only one cannot be edited by a screen that forgot to check.
 */
// Where the caret goes when the field is clicked at nX. The inset the theme
// draws the text at lives in one place, so a click lands where the letters
// actually are and not where a second copy of the arithmetic thought they were.
void WidgetFieldClick (TWidget *pWidget, const TTheme *pTheme, int nX);

void WidgetFieldInsert (TWidget *pWidget, unsigned nCode);
void WidgetFieldBackspace (TWidget *pWidget);
void WidgetFieldDelete (TWidget *pWidget);
void WidgetFieldCaret (TWidget *pWidget, int nStep);    // -1 left, +1 right
void WidgetFieldHome (TWidget *pWidget);
void WidgetFieldEnd (TWidget *pWidget);

#endif
