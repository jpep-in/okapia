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
    // The same thing, tighter, and never in a screen's array: a menu is a list
    // that appears over everything and goes away again.
    WidgetMenu,
    WidgetScrollbar,
    WidgetPopup,
    WidgetField,
    WidgetProgress,
    WidgetSeparator
};

// The columns a list may carry beyond its name, and how many there can be. Two
// is what the chooser needs — startup and mounted — because a cell can only be
// ticked, and *how* a volume is mounted is a choice of three (hard disk, hard
// disk read-only, CD-ROM) that a tick box cannot hold. That one went to a popup
// under the list rather than becoming a cell type: a menu opening over a list
// that scrolls is a different component, and a list of columns is a table,
// which is another one again. When a third column is wanted, that is the
// conversation to have rather than raising this number.
static const unsigned LIST_COLUMNS = 2;

struct TListColumn
{
    const char *pHeader;
    bool        bRadio;                 // one of a set; otherwise a tick box
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
    // One state per column: StateChecked and StateDisabled, per row. A cell
    // that cannot be operated on this row says so by going grey, which is what
    // the three tick boxes under the list used to do from a distance.
    unsigned           nCell[LIST_COLUMNS];
    // A row that is a command rather than a subject: "New volume…" among the
    // volumes. It has no answers, so it draws no marks and the keyboard cannot
    // walk into a column on it, and it activates on a single click where an
    // ordinary row only becomes the selection — because there is nothing else
    // clicking it could mean. It lives in the list rather than beside it
    // because what it makes appears in the list.
    bool               bAction;
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
    const TListColumn  *pColumns;       // 0 for a plain list
    unsigned            nColumns;
    // Which column the keyboard is on; 0 is the name. Left and right walk it,
    // so a list of tick boxes is operable without a mouse — which the three
    // controls under the chooser's list used to provide by being components of
    // their own.
    unsigned            nCell;

    // A field's own storage. A field without it is read-only, which is what
    // every other control is.
    char               *pEdit;
    unsigned            nEditSize;      // bytes available, terminator included
    unsigned            nCaret;         // the insertion point, as a byte offset
    unsigned            nAnchor;        // where a selection began; equal when none
};

// Fills one in with nothing set. Every screen builds its components through
// this: a field left uninitialised in a struct this wide is a pointer nobody
// wrote, and it would be read the first time somebody scrolled.
void WidgetClear (TWidget *pWidget);

// States a screen restates from its model, keeping the ones that are not its to
// give. The focus and the caret belong to the loop, not to the page: a screen
// that assigns a whole state wipes the ring the loop has just set — and since
// the loop asks the page to restate itself on every change, the ring was gone
// before it was ever drawn. That is exactly how the main screen's Start button
// could never be seen to hold the keyboard.
//
// One function rather than the idiom repeated at each site: it was already
// written by hand in three places on the settings page and nowhere else, which
// is what a rule looks like just before it stops being kept.
void WidgetSetState (TWidget *pWidget, unsigned nState);

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

// Which column a point is in: 0 for the name, 1..nColumns for the marks. It
// answers a column even for a point in the heading, so a caller that wants a
// row must ask WidgetListItemAt as well.
unsigned WidgetListColumnAt (const TWidget *pWidget, const TTheme *pTheme, int nX);


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
void WidgetFieldClick (TWidget *pWidget, const TTheme *pTheme, int nX, bool bExtend);

// The caret and the anchor, as the theme wants them.
TFieldMark WidgetFieldMark (const TWidget *pWidget);

// bExtend keeps the anchor where it is, which is what Shift does everywhere.
void WidgetFieldInsert (TWidget *pWidget, unsigned nCode);
void WidgetFieldBackspace (TWidget *pWidget);
void WidgetFieldDelete (TWidget *pWidget);
void WidgetFieldCaret (TWidget *pWidget, int nStep, bool bExtend);
void WidgetFieldHome (TWidget *pWidget, bool bExtend);
void WidgetFieldEnd (TWidget *pWidget, bool bExtend);

/*
 *  Selection, and the scrap
 *
 *  One scrap for the whole firmware, fixed and small: there is one field on a
 *  screen and nothing here allocates. It is the Clipboard in the only sense
 *  this machine needs — a place text waits between two of its own fields.
 */
void WidgetFieldSelectAll (TWidget *pWidget);
bool WidgetFieldHasSelection (const TWidget *pWidget);
void WidgetFieldCopy (const TWidget *pWidget);
void WidgetFieldCut (TWidget *pWidget);
void WidgetFieldPaste (TWidget *pWidget);

#endif
