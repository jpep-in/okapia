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
 * Copyright (C) 2026  Okapia contributors
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
};

// Takes the focus from the screen if it declares one, and otherwise puts it on
// the first control that can hold it. A screen with none — an alert that only
// waits for Return — is a legitimate case and not an error.
void ScreenInit (TScreen *pScreen, const TTheme *pTheme, TWidget *pWidgets, unsigned nCount);

// One event in, one answer out. Never draws: the caller repaints when the
// answer says something changed, which is also what keeps a moving pointer from
// costing a full repaint at 1920x1080.
TScreenReply ScreenEvent (TScreen *pScreen, const TEvent *pEvent);

// Operates a control as if the user had: toggles a tick box, picks a radio out
// of its group, selects a row. Exposed because a screen sometimes has to do it
// itself — a default that must be shown as chosen before anyone has touched it.
void ScreenOperate (TScreen *pScreen, int nIndex);

#endif
