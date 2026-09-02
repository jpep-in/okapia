/*
 * okapia_screen.cpp — a screen is an array of components and a loop over events.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_screen.h"

static TScreenReply Reply (TScreenResult Result, int nIndex)
{
    TScreenReply r = { Result, nIndex };
    return r;
}

// nStep is +1 or -1; wraps, and gives up rather than spinning if nothing at all
// can hold the focus.
static int NextFocus (const TScreen *pScreen, int nFrom, int nStep)
{
    if (pScreen->nCount == 0)
    {
        return -1;
    }
    int i = nFrom;
    for (unsigned n = 0; n < pScreen->nCount; n++)
    {
        i += nStep;
        if (i < 0)
        {
            i = (int) pScreen->nCount - 1;
        }
        else if (i >= (int) pScreen->nCount)
        {
            i = 0;
        }
        if (WidgetFocusable (&pScreen->pWidgets[i]))
        {
            return i;
        }
    }
    return -1;
}

// The one control carrying StateDefault, which Return operates wherever the
// focus happens to be. There is at most one: a screen with two of them would
// draw two rings and mean neither.
static int DefaultWidget (const TScreen *pScreen)
{
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        if ((pScreen->pWidgets[i].nState & StateDefault)
            && !(pScreen->pWidgets[i].nState & StateDisabled))
        {
            return (int) i;
        }
    }
    return -1;
}

static void SetFocus (TScreen *pScreen, int nIndex)
{
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        pScreen->pWidgets[i].nState &= ~(unsigned) StateFocused;
    }
    if (nIndex >= 0)
    {
        pScreen->pWidgets[nIndex].nState |= StateFocused;
    }
    pScreen->nFocus = nIndex;
}

void ScreenInit (TScreen *pScreen, const TTheme *pTheme, TWidget *pWidgets, unsigned nCount)
{
    pScreen->pWidgets = pWidgets;
    pScreen->nCount   = nCount;
    pScreen->pTheme   = pTheme;
    pScreen->nFocus   = -1;
    pScreen->nPressed = -1;
    pScreen->nX       = 0;
    pScreen->nY       = 0;

    // A screen that says where its focus starts is obeyed — an alert wants it on
    // its field, not on whichever control it happened to place first. Failing
    // that, backwards from -1 lands on index 0, so "the first that can hold it"
    // means the first in the array and not the last.
    //
    // Nothing is cleared here. Tidying the array is what SetFocus does from the
    // first Tab onwards; doing it at once would quietly rewrite a screen its
    // author had already drawn and checked, which is not this function's to do.
    for (unsigned i = 0; i < nCount; i++)
    {
        if ((pWidgets[i].nState & StateFocused) && WidgetFocusable (&pWidgets[i]))
        {
            pScreen->nFocus = (int) i;
            return;
        }
    }
    SetFocus (pScreen, NextFocus (pScreen, -1, +1));
}

void ScreenOperate (TScreen *pScreen, int nIndex)
{
    if (nIndex < 0 || nIndex >= (int) pScreen->nCount)
    {
        return;
    }
    TWidget *p = &pScreen->pWidgets[nIndex];

    switch (p->Type)
    {
    case WidgetCheckbox:
        p->nState ^= (unsigned) StateChecked;
        break;

    case WidgetRadio:
        // Its group and no other. Clearing every radio on the screen is the
        // obvious version and it is wrong the day a screen has two questions.
        for (unsigned i = 0; i < pScreen->nCount; i++)
        {
            if (pScreen->pWidgets[i].Type == WidgetRadio
                && pScreen->pWidgets[i].nGroup == p->nGroup)
            {
                pScreen->pWidgets[i].nState &= ~(unsigned) StateChecked;
            }
        }
        p->nState |= StateChecked;
        break;

    default:
        // A button, a pop-up, a field: operating it is the caller's business,
        // and it hears about it through ScreenActivated.
        break;
    }
}

// Moves a list's choice by nStep items, stepping over the disabled ones and
// stopping at the ends. No wrapping: a list that jumps from the last volume
// back to the first reads as a glitch, and the Mac's own lists stopped too.
static bool ListMove (TScreen *pScreen, int nList, int nStep)
{
    TWidget *p = &pScreen->pWidgets[nList];
    if (p->nItems == 0)
    {
        return false;                   // an empty list: the card may have none
    }

    int nWanted = p->nChoice;
    if (nWanted < 0)
    {
        nWanted = nStep > 0 ? -1 : (int) p->nItems;
    }
    for (;;)
    {
        nWanted += nStep > 0 ? 1 : -1;
        if (nWanted < 0 || nWanted >= (int) p->nItems)
        {
            return false;
        }
        if (!(p->pItems[nWanted].nState & StateDisabled))
        {
            break;
        }
    }
    // A page's worth at a time is the same walk repeated, so that it lands on
    // something choosable rather than on whatever happens to be a page away.
    int nLeft = (nStep > 0 ? nStep : -nStep) - 1;
    while (nLeft-- > 0)
    {
        int nTry = nWanted;
        for (;;)
        {
            nTry += nStep > 0 ? 1 : -1;
            if (nTry < 0 || nTry >= (int) p->nItems)
            {
                nTry = -1;
                break;
            }
            if (!(p->pItems[nTry].nState & StateDisabled))
            {
                break;
            }
        }
        if (nTry < 0)
        {
            break;
        }
        nWanted = nTry;
    }

    if (nWanted == p->nChoice)
    {
        return false;
    }
    p->nChoice = nWanted;
    WidgetListReveal (p, pScreen->pTheme);
    return true;
}

static bool ListChoose (TScreen *pScreen, int nList, int nItem)
{
    TWidget *p = &pScreen->pWidgets[nList];
    if (nItem < 0 || nItem >= (int) p->nItems
        || (p->pItems[nItem].nState & StateDisabled)
        || p->nChoice == nItem)
    {
        return false;
    }
    p->nChoice = nItem;
    WidgetListReveal (p, pScreen->pTheme);
    return true;
}

static void SetPressed (TScreen *pScreen, int nIndex, bool bPressed)
{
    if (nIndex < 0)
    {
        return;
    }
    if (bPressed)
    {
        pScreen->pWidgets[nIndex].nState |= StatePressed;
    }
    else
    {
        pScreen->pWidgets[nIndex].nState &= ~(unsigned) StatePressed;
    }
}

// Everything a field answers to. Kept apart because a focused field takes keys
// that mean something else anywhere on the screen — Left and Right move a caret
// there and nothing at all elsewhere — and burying that in one switch is how a
// screen ends up doing two things for one key.
static TScreenReply HandleField (TScreen *pScreen, TWidget *p, const TEvent *pEvent)
{
    switch (pEvent->nKey)
    {
    case OkKeyBackspace:  WidgetFieldBackspace (p); return Reply (ScreenChanged, -1);
    case OkKeyDelete:     WidgetFieldDelete (p);    return Reply (ScreenChanged, -1);
    case OkKeyLeft:       WidgetFieldCaret (p, -1); return Reply (ScreenChanged, -1);
    case OkKeyRight:      WidgetFieldCaret (p, +1); return Reply (ScreenChanged, -1);
    case OkKeyHome:       WidgetFieldHome (p);      return Reply (ScreenChanged, -1);
    case OkKeyEnd:        WidgetFieldEnd (p);       return Reply (ScreenChanged, -1);
    default:
        break;
    }
    // Anything that produced a character. Tab and Return produce none, so they
    // fall through to the screen and keep meaning what they mean everywhere.
    if (pEvent->nChar != 0 && !(pEvent->nModifiers & (ModCommand | ModControl)))
    {
        WidgetFieldInsert (p, pEvent->nChar);
        return Reply (ScreenChanged, -1);
    }
    return Reply (ScreenIdle, -1);
}

static TScreenReply HandleKey (TScreen *pScreen, const TEvent *pEvent)
{
    const int nFocused = pScreen->nFocus;
    TWidget *p = nFocused >= 0 ? &pScreen->pWidgets[nFocused] : 0;

    // A field that can be edited answers first, and only says nothing when the
    // key was not one of its own.
    if (p != 0 && p->Type == WidgetField && p->pEdit != 0)
    {
        const TScreenReply Field = HandleField (pScreen, p, pEvent);
        if (Field.Result != ScreenIdle)
        {
            return Field;
        }
    }

    switch (pEvent->nKey)
    {
    case OkKeyTab:
        {
            const int nStep = (pEvent->nModifiers & ModShift) ? -1 : +1;
            const int nNext = NextFocus (pScreen, pScreen->nFocus, nStep);
            if (nNext < 0 || nNext == pScreen->nFocus)
            {
                return Reply (ScreenIdle, -1);
            }
            SetFocus (pScreen, nNext);
            return Reply (ScreenChanged, -1);
        }

    case OkKeyUp:
    case OkKeyDown:
    case OkKeyPageUp:
    case OkKeyPageDown:
    case OkKeyHome:
    case OkKeyEnd:
        // Arrows belong to the list, and to a field's caret. Nowhere else do
        // they do anything, on purpose: making them a second focus ring means a
        // screen behaves differently depending on where the focus already is,
        // which is the kind of rule nobody can hold in their head.
        if (nFocused >= 0 && p->Type == WidgetList)
        {
            const int nPage = (int) WidgetListVisible (p, pScreen->pTheme);
            int nStep;
            switch (pEvent->nKey)
            {
            case OkKeyUp:       nStep = -1; break;
            case OkKeyDown:     nStep = +1; break;
            case OkKeyPageUp:   nStep = -(nPage > 1 ? nPage - 1 : 1); break;
            case OkKeyPageDown: nStep = +(nPage > 1 ? nPage - 1 : 1); break;
            default:            nStep = pEvent->nKey == OkKeyHome
                                      ? -(int) p->nItems : +(int) p->nItems; break;
            }
            return Reply (ListMove (pScreen, nFocused, nStep) ? ScreenChanged : ScreenIdle,
                          -1);
        }
        return Reply (ScreenIdle, -1);

    case OkKeySpace:
        // Operates whatever holds the focus. A button has nothing to toggle, so
        // for it this is simply the press.
        if (pScreen->nFocus < 0)
        {
            return Reply (ScreenIdle, -1);
        }
        ScreenOperate (pScreen, pScreen->nFocus);
        return Reply (ScreenActivated, pScreen->nFocus);

    case OkKeyReturn:
        {
            // The default button, wherever the focus is — that is what a ring
            // around it promises. Space is the one that follows the focus, and
            // keeping the two apart is what makes either of them predictable.
            const int nDefault = DefaultWidget (pScreen);
            if (nDefault < 0)
            {
                return Reply (ScreenIdle, -1);
            }
            ScreenOperate (pScreen, nDefault);
            return Reply (ScreenActivated, nDefault);
        }

    case OkKeyEscape:
        return Reply (ScreenCancelled, -1);

    default:
        return Reply (ScreenIdle, -1);
    }
}

TScreenReply ScreenEvent (TScreen *pScreen, const TEvent *pEvent)
{
    switch (pEvent->Type)
    {
    case EventKeyDown:
        return HandleKey (pScreen, pEvent);

    case EventMouseMove:
        {
            pScreen->nX = pEvent->nX;
            pScreen->nY = pEvent->nY;
            if (pScreen->nPressed < 0)
            {
                return Reply (ScreenIdle, -1);
            }
            // Dragging out of a control it is holding lets go of it visually
            // and takes it back on the way in — TrackControl, and the reason a
            // mistaken click on a Macintosh could always be taken back.
            const bool bInside = RectContains (pScreen->pWidgets[pScreen->nPressed].Rect,
                                               pEvent->nX, pEvent->nY);
            const bool bShown  = (pScreen->pWidgets[pScreen->nPressed].nState
                                  & StatePressed) != 0;
            if (bInside == bShown)
            {
                return Reply (ScreenIdle, -1);
            }
            SetPressed (pScreen, pScreen->nPressed, bInside);
            return Reply (ScreenChanged, -1);
        }

    case EventMouseDown:
        {
            pScreen->nX = pEvent->nX;
            pScreen->nY = pEvent->nY;
            const int nHit = WidgetHit (pScreen->pWidgets, pScreen->nCount,
                                        pEvent->nX, pEvent->nY);
            if (nHit < 0)
            {
                return Reply (ScreenIdle, -1);
            }
            // A row gives the focus to its list, not to itself: the focus rests
            // on things the keyboard can then move around in.
            if (WidgetFocusable (&pScreen->pWidgets[nHit]))
            {
                SetFocus (pScreen, nHit);
            }
            // A click inside a list picks the row it landed on, there and then:
            // a list that only answers on release cannot be dragged through,
            // which is how one has always been read.
            if (pScreen->pWidgets[nHit].Type == WidgetList)
            {
                ListChoose (pScreen, nHit,
                            WidgetListItemAt (&pScreen->pWidgets[nHit], pScreen->pTheme,
                                              pEvent->nX, pEvent->nY));
            }
            pScreen->nPressed = nHit;
            SetPressed (pScreen, nHit, true);
            return Reply (ScreenChanged, -1);
        }

    case EventMouseUp:
        {
            pScreen->nX = pEvent->nX;
            pScreen->nY = pEvent->nY;
            const int nPressed = pScreen->nPressed;
            if (nPressed < 0)
            {
                return Reply (ScreenIdle, -1);
            }
            SetPressed (pScreen, nPressed, false);
            pScreen->nPressed = -1;
            if (!RectContains (pScreen->pWidgets[nPressed].Rect, pEvent->nX, pEvent->nY))
            {
                return Reply (ScreenChanged, -1);       // let go outside: taken back
            }
            ScreenOperate (pScreen, nPressed);
            return Reply (ScreenActivated, nPressed);
        }

    default:
        return Reply (ScreenIdle, -1);
    }
}
