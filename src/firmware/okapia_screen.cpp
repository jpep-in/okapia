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

void ScreenTouch (TScreen *pScreen, int nIndex)
{
    if (nIndex < 0 || pScreen->bDirtyAll)
    {
        return;
    }
    for (unsigned i = 0; i < pScreen->nDirtyCount; i++)
    {
        if (pScreen->nDirty[i] == (unsigned) nIndex)
        {
            return;
        }
    }
    if (pScreen->nDirtyCount == sizeof pScreen->nDirty / sizeof pScreen->nDirty[0])
    {
        pScreen->bDirtyAll = true;      // more than it is worth tracking
        return;
    }
    pScreen->nDirty[pScreen->nDirtyCount++] = (unsigned) nIndex;
}

static void SetFocus (TScreen *pScreen, int nIndex)
{
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        if (pScreen->pWidgets[i].nState & StateFocused)
        {
            pScreen->pWidgets[i].nState &= ~(unsigned) StateFocused;
            ScreenTouch (pScreen, (int) i);
        }
    }
    if (nIndex >= 0)
    {
        pScreen->pWidgets[nIndex].nState |= StateFocused;
        ScreenTouch (pScreen, nIndex);
    }
    pScreen->nFocus = nIndex;
}

bool ScreenPaintDirty (TSurface *pSurface, TScreen *pScreen)
{
    if (pScreen->bDirtyAll)
    {
        pScreen->bDirtyAll   = false;
        pScreen->nDirtyCount = 0;
        return false;                   // the caller redraws the lot
    }
    // The widest a control ever draws beyond itself, whatever state it is in.
    // Asking per control would be a pixel tighter and wrong the moment one
    // gains a state between two paints.
    const unsigned nReach = ThemeReach (pScreen->pTheme, StateDefault | StateFocused);
    for (unsigned i = 0; i < pScreen->nDirtyCount; i++)
    {
        const TWidget *p = &pScreen->pWidgets[pScreen->nDirty[i]];
        GfxFill (pSurface, RectInset (p->Rect, -(int) nReach, -(int) nReach),
                 pScreen->Background);
        WidgetDraw (pSurface, pScreen->pTheme, p);
    }
    pScreen->nDirtyCount = 0;
    return true;
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
    pScreen->nDragList   = -1;
    pScreen->nDragGrab   = 0;
    pScreen->nDirtyCount = 0;
    pScreen->bDirtyAll   = true;        // nothing has been drawn yet
    pScreen->Background  = ColorWhite;

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
    ScreenTouch (pScreen, nIndex);

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
                && pScreen->pWidgets[i].nGroup == p->nGroup
                && (pScreen->pWidgets[i].nState & StateChecked))
            {
                pScreen->pWidgets[i].nState &= ~(unsigned) StateChecked;
                ScreenTouch (pScreen, (int) i);
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
    ScreenTouch (pScreen, nList);
    return true;
}

// The thumb inside a list's scroller, from the one place that works it out.
static bool ThumbOf (const TWidget *p, const TTheme *pTheme, TRect *pOut)
{
    const TRect Bar = WidgetListScroller (p, pTheme);
    if (Bar.nWidth == 0)
    {
        return false;
    }
    *pOut = ThemeScrollThumb (pTheme, Bar, p->nTop, WidgetListVisible (p, pTheme),
                              p->nItems);
    return pOut->nHeight != 0;
}

// Puts the top row where a thumb whose top is at nY would put it.
static bool ScrollTo (TScreen *pScreen, int nList, int nY)
{
    TWidget *p = &pScreen->pWidgets[nList];
    TRect Thumb;
    if (!ThumbOf (p, pScreen->pTheme, &Thumb))
    {
        return false;
    }
    const TRect Bar = WidgetListScroller (p, pScreen->pTheme);
    const unsigned nVisible = WidgetListVisible (p, pScreen->pTheme);
    const unsigned nSteps = p->nItems - nVisible;
    const int nTravel = (int) (Bar.nHeight - 2 - Thumb.nHeight);
    if (nTravel <= 0)
    {
        return false;
    }
    int nWanted = ((nY - (Bar.nY + 1)) * (int) nSteps + nTravel / 2) / nTravel;
    if (nWanted < 0)                nWanted = 0;
    if (nWanted > (int) nSteps)     nWanted = (int) nSteps;
    if ((unsigned) nWanted == p->nTop)
    {
        return false;
    }
    p->nTop = (unsigned) nWanted;
    ScreenTouch (pScreen, nList);
    return true;
}

// A click in the scroller: on the thumb it takes hold of it, above or below it
// moves by a page — which is what a Macintosh's scroll bar did, and what keeps
// a click from throwing the reader somewhere they did not aim for.
static TScreenReply ScrollerClick (TScreen *pScreen, int nList, int nX, int nY)
{
    TWidget *p = &pScreen->pWidgets[nList];
    TRect Thumb;
    if (!ThumbOf (p, pScreen->pTheme, &Thumb))
    {
        return Reply (ScreenIdle, -1);
    }
    if (RectContains (Thumb, nX, nY))
    {
        pScreen->nDragList = nList;
        pScreen->nDragGrab = nY - Thumb.nY;
        return Reply (ScreenIdle, -1);
    }

    const unsigned nVisible = WidgetListVisible (p, pScreen->pTheme);
    const unsigned nPage = nVisible > 1 ? nVisible - 1 : 1;
    const unsigned nSteps = p->nItems - nVisible;
    unsigned nWanted;
    if (nY < Thumb.nY)
    {
        nWanted = p->nTop > nPage ? p->nTop - nPage : 0;
    }
    else
    {
        nWanted = p->nTop + nPage > nSteps ? nSteps : p->nTop + nPage;
    }
    if (nWanted == p->nTop)
    {
        return Reply (ScreenIdle, -1);
    }
    p->nTop = nWanted;
    ScreenTouch (pScreen, nList);
    return Reply (ScreenChanged, -1);
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
    ScreenTouch (pScreen, nList);
    return true;
}

static void SetPressed (TScreen *pScreen, int nIndex, bool bPressed)
{
    if (nIndex < 0)
    {
        return;
    }
    ScreenTouch (pScreen, nIndex);
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
    bool bMine = true;
    switch (pEvent->nKey)
    {
    case OkKeyBackspace:  WidgetFieldBackspace (p); break;
    case OkKeyDelete:     WidgetFieldDelete (p);    break;
    case OkKeyLeft:       WidgetFieldCaret (p, -1); break;
    case OkKeyRight:      WidgetFieldCaret (p, +1); break;
    case OkKeyHome:       WidgetFieldHome (p);      break;
    case OkKeyEnd:        WidgetFieldEnd (p);       break;
    default:
        // Anything that produced a character. Tab and Return produce none, so
        // they fall through to the screen and keep meaning what they mean
        // everywhere; Command and Control combinations are not text either.
        bMine = pEvent->nChar != 0
             && !(pEvent->nModifiers & (ModCommand | ModControl));
        if (bMine)
        {
            WidgetFieldInsert (p, pEvent->nChar);
        }
        break;
    }
    if (!bMine)
    {
        return Reply (ScreenIdle, -1);
    }
    ScreenTouch (pScreen, pScreen->nFocus);
    return Reply (ScreenChanged, -1);
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
            if (pScreen->nDragList >= 0)
            {
                return Reply (ScrollTo (pScreen, pScreen->nDragList,
                                        pEvent->nY - pScreen->nDragGrab)
                                  ? ScreenChanged : ScreenIdle, -1);
            }
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
            // which is how one has always been read. In its scroller it scrolls
            // instead, and picks nothing.
            if (pScreen->pWidgets[nHit].Type == WidgetList)
            {
                const TRect Bar = WidgetListScroller (&pScreen->pWidgets[nHit],
                                                      pScreen->pTheme);
                if (Bar.nWidth != 0 && RectContains (Bar, pEvent->nX, pEvent->nY))
                {
                    ScrollerClick (pScreen, nHit, pEvent->nX, pEvent->nY);
                    pScreen->nPressed = nHit;
                    return Reply (ScreenChanged, -1);
                }
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
            if (pScreen->nDragList >= 0)
            {
                pScreen->nDragList = -1;
                pScreen->nPressed  = -1;
                return Reply (ScreenIdle, -1);
            }
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
