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

// A list owns the rows that lie within it. Geometry and not a field, because
// the rows are inside the frame on screen too: a rule that can be seen is a
// rule that cannot drift out of step with what is drawn.
static bool RowBelongsTo (const TWidget *pRow, const TWidget *pFrame)
{
    return pRow->Type == WidgetListRow
        && pRow->Rect.nX >= pFrame->Rect.nX
        && pRow->Rect.nY >= pFrame->Rect.nY
        && pRow->Rect.nX + (int) pRow->Rect.nWidth
               <= pFrame->Rect.nX + (int) pFrame->Rect.nWidth
        && pRow->Rect.nY + (int) pRow->Rect.nHeight
               <= pFrame->Rect.nY + (int) pFrame->Rect.nHeight;
}

// The list a row is in, or -1. Rows are hit before their frame, so a click has
// to find its way back up.
static int ListOwning (const TScreen *pScreen, int nRow)
{
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        if (pScreen->pWidgets[i].Type == WidgetListFrame
            && RowBelongsTo (&pScreen->pWidgets[nRow], &pScreen->pWidgets[i]))
        {
            return (int) i;
        }
    }
    return -1;
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

void ScreenInit (TScreen *pScreen, TWidget *pWidgets, unsigned nCount)
{
    pScreen->pWidgets = pWidgets;
    pScreen->nCount   = nCount;
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

int ScreenSelectedRow (const TScreen *pScreen, int nList)
{
    if (nList < 0)
    {
        return -1;
    }
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        if ((pScreen->pWidgets[i].nState & StateSelected)
            && RowBelongsTo (&pScreen->pWidgets[i], &pScreen->pWidgets[nList]))
        {
            return (int) i;
        }
    }
    return -1;
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

    case WidgetListRow:
        {
            const int nList = ListOwning (pScreen, nIndex);
            for (unsigned i = 0; i < pScreen->nCount; i++)
            {
                if (nList < 0
                    || RowBelongsTo (&pScreen->pWidgets[i], &pScreen->pWidgets[nList]))
                {
                    pScreen->pWidgets[i].nState &= ~(unsigned) StateSelected;
                }
            }
            p->nState |= StateSelected;
        }
        break;

    default:
        // A button, a pop-up, a field: operating it is the caller's business,
        // and it hears about it through ScreenActivated.
        break;
    }
}

// Moves the selection within a list by nStep rows, in array order. Disabled
// rows are stepped over rather than landed on.
static bool MoveSelection (TScreen *pScreen, int nList, int nStep)
{
    int nFirst = -1, nLast = -1;
    for (unsigned i = 0; i < pScreen->nCount; i++)
    {
        if (RowBelongsTo (&pScreen->pWidgets[i], &pScreen->pWidgets[nList])
            && !(pScreen->pWidgets[i].nState & StateDisabled))
        {
            if (nFirst < 0)
            {
                nFirst = (int) i;
            }
            nLast = (int) i;
        }
    }
    if (nFirst < 0)
    {
        return false;                   // an empty list: the card may have none
    }

    const int nCurrent = ScreenSelectedRow (pScreen, nList);
    int nWanted;
    if (nCurrent < 0)
    {
        nWanted = nStep > 0 ? nFirst : nLast;
    }
    else
    {
        // No wrapping. A list that jumps from the last volume back to the first
        // reads as a glitch, and the Mac's own lists stopped at the end too.
        nWanted = nCurrent;
        for (int n = 0; n < (int) pScreen->nCount; n++)
        {
            nWanted += nStep;
            if (nWanted < nFirst || nWanted > nLast)
            {
                return false;
            }
            if (RowBelongsTo (&pScreen->pWidgets[nWanted], &pScreen->pWidgets[nList])
                && !(pScreen->pWidgets[nWanted].nState & StateDisabled))
            {
                break;
            }
        }
    }
    if (nWanted == nCurrent)
    {
        return false;
    }
    ScreenOperate (pScreen, nWanted);
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

static TScreenReply HandleKey (TScreen *pScreen, const TEvent *pEvent)
{
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
        // Arrows belong to the list. Anywhere else they do nothing, on purpose:
        // making them a second focus ring means a screen behaves differently
        // depending on where the focus already is, which is the kind of rule
        // nobody can hold in their head.
        if (pScreen->nFocus >= 0
            && pScreen->pWidgets[pScreen->nFocus].Type == WidgetListFrame)
        {
            const int nStep = pEvent->nKey == OkKeyDown ? +1 : -1;
            return Reply (MoveSelection (pScreen, pScreen->nFocus, nStep)
                              ? ScreenChanged : ScreenIdle, -1);
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
            int nFocus = nHit;
            if (pScreen->pWidgets[nHit].Type == WidgetListRow)
            {
                nFocus = ListOwning (pScreen, nHit);
            }
            if (nFocus >= 0 && WidgetFocusable (&pScreen->pWidgets[nFocus]))
            {
                SetFocus (pScreen, nFocus);
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
