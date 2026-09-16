/*
 * okapia_screen.h — a screen is an array of components and a loop over events.
 *
 * This is ModalDialog, and it is the whole of the interface's behaviour: where
 * the focus is, which control the mouse is holding, what a key does. The
 * screens above it (the chooser, the settings, the repair alert) say what their
 * components are and what to do when one is operated; they do not each rewrite
 * this.
 *
 * It touches no pixels and no Circle type, which is what lets the behaviour be
 * tested with synthetic events on a development machine — the first part of the
 * firmware that can be checked without a screen at all (tests/host/).
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_screen_h
#define _okapia_screen_h

#include "okapia_event.h"
#include "okapia_widgets.h"

struct TScreen
{
    TWidget      *pWidgets;             // the screen's own array, and it is mutable:
    unsigned      nCount;               // the screen places controls, the loop states them
    const TTheme *pTheme;               // a list's rows are geometry only it knows
    int           nFocus;               // index, or -1 while nothing is focused
    int           nPressed;             // control the mouse is holding down, or -1
    int           nX;                   // where the pointer was last seen
    int           nY;

    TRect         Bounds;               // where a menu may open without leaving the screen

    /*
     *  The pop-up menu that is open, if one is
     *
     *  A menu is a list that appears over the screen and goes away again — so it
     *  is a list, and it lives here rather than in the array, because nothing
     *  else on the screen may be laid out around something that is not there
     *  most of the time.
     */
    int           nMenu;                // the pop-up it belongs to, or -1
    TWidget       Menu;                 // the list it shows while it is open
    // Where the pointer stood when it opened, and whether the click that opened
    // it has been let go yet. A menu opened by a click has to stay open: the
    // release ending that click must not choose, or it flashes and is gone and
    // the only way to use it is to keep the button down. Dragging into it and
    // releasing still chooses, which is the other way a pop-up has always been
    // used, and both are told apart by whether the pointer moved.
    int           nMenuOpenX;
    int           nMenuOpenY;
    bool          bMenuHeld;

    // Dragging a list's scroller, and where inside its thumb it was taken hold
    // of. Grabbing a thumb by its middle whatever part of it was clicked makes
    // it jump under the hand at the first pixel of movement.
    int           nDragList;            // the list being scrolled, or -1
    int           nDragGrab;

    /*
     *  What has changed since the last paint
     *
     *  A screen redrawn whole on every keystroke flickers, and under a window
     *  it costs far more than the machine has: a full repaint at 1280x960 runs
     *  into tens of milliseconds, the pointer visibly stops while it happens,
     *  and the ground being painted back in before the controls makes the whole
     *  surface blink. The Mac's own compositor answers this the same way, and
     *  the check that no two controls overlap is what makes redrawing one of
     *  them on its own safe.
     */
    // The caret blinks, as it always has: a mark that never moves is hard to
    // find in a line of text, and a Macintosh gave the rate its own setting.
    bool          bCaret;

    unsigned      nDirty[12];
    unsigned      nDirtyCount;
    bool          bDirtyAll;            // the caller must redraw everything
    TOkapiaColor  Background;           // what a control stands on
};

enum TScreenResult
{
    ScreenIdle,                         // nothing to redraw
    ScreenChanged,                      // state moved: repaint
    ScreenActivated,                    // a control was operated; nIndex says which
    ScreenCancelled                     // Escape
};

struct TScreenReply
{
    TScreenResult Result;
    int           nIndex;               // meaningful for ScreenActivated
    // Which column of a list was operated: 0 for the row itself, 1..nColumns
    // for one of its marks. A list with no columns always answers 0, so a
    // screen that has never heard of columns reads exactly as it did.
    unsigned      nCell;
};

// Takes the focus from the screen if it declares one, and otherwise puts it on
// the first control that can hold it. A screen with none — an alert that only
// waits for Return — is a legitimate case and not an error.
void ScreenInit (TScreen *pScreen, const TTheme *pTheme, TWidget *pWidgets, unsigned nCount);

// Marks a control as needing to be drawn again. Every change of state inside
// this file goes through it; a screen that changes one itself owes the call.
void ScreenTouch (TScreen *pScreen, int nIndex);

// Draws again only what changed, putting the ground back under each control
// first. Answers false when too much has changed to be worth tracking, in which
// case the caller redraws the screen whole — that is the first paint and a
// change of page, and nothing else in ordinary use.
// pDamage is filled with the rectangle that was touched, so a caller drawing
// into a shadow surface knows exactly what to copy to the screen.
bool ScreenPaintDirty (TSurface *pSurface, TScreen *pScreen, TRect *pDamage);

// Draws the open menu, if there is one, over whatever is already there. The
// incremental paint does this itself; a caller that redrew the whole screen
// owes the call, since a menu is the one thing here that stands in front and
// the screen it stands in front of has just been painted over it.
void ScreenPaintMenu (TSurface *pSurface, TScreen *pScreen, TRect *pDamage);

/*
 *  One frame, from the screen to the glass
 *
 *  The pointer taken down, what changed drawn again — or everything, through
 *  pRepaint, when the screen says so — the pointer put back, and only the part
 *  that moved copied forward. Every subtlety about tearing, the save-under and
 *  the menu that stands in front lives here instead of in each caller's loop,
 *  which is where two of them would slowly stop agreeing.
 *
 *  pShadow is where everything is drawn and pOutput is the frame buffer. They
 *  must be the same size; nothing is ever drawn straight into pOutput, because
 *  painting the ground before the control that stands on it is visible at sixty
 *  refreshes a second.
 */
void ScreenPresent (TSurface *pShadow, TSurface *pOutput, TScreen *pScreen,
                    void (*pRepaint) (TSurface *), bool bPointer, int nX, int nY,
                    unsigned nCursorScale);

// Whether a pop-up's menu is open. The loop asks in order to know that closing
// it will need the screen underneath drawn again.
bool ScreenMenuOpen (const TScreen *pScreen);

// Turns the caret on or off. The loop calls it on the beat it likes — half a
// second is what a Macintosh shipped with — and it answers whether anything
// changed, which is nothing at all unless a field holds the focus.
bool ScreenBlinkCaret (TScreen *pScreen);

// The pointer's shape for what lies under it. A text field asks for the beam,
// as it has since 1984, and the answer belongs here because only the screen
// knows what its controls are.
enum TCursorShape
{
    CursorArrow,
    CursorBeam
};
TCursorShape ScreenCursorAt (const TScreen *pScreen, int nX, int nY);

// One event in, one answer out. Never draws: the caller repaints when the
// answer says something changed, which is also what keeps a moving pointer from
// costing a full repaint at 1920x1080.
TScreenReply ScreenEvent (TScreen *pScreen, const TEvent *pEvent);

// Operates a control as if the user had: toggles a tick box, picks a radio out
// of its group, selects a row. Exposed because a screen sometimes has to do it
// itself — a default that must be shown as chosen before anyone has touched it.
void ScreenOperate (TScreen *pScreen, int nIndex);

#endif
