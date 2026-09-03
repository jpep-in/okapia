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
            pScreen->pWidgets[i].nState &= ~(unsigned) (StateFocused | StateCaret);
            ScreenTouch (pScreen, (int) i);
        }
    }
    if (nIndex >= 0)
    {
        // Lit as it arrives: a caret that waits for the next beat leaves the
        // field looking inert for up to half a second after it is clicked.
        pScreen->pWidgets[nIndex].nState |= StateFocused | StateCaret;
        ScreenTouch (pScreen, nIndex);
        pScreen->bCaret = true;
    }
    pScreen->nFocus = nIndex;
}

void ScreenPaintMenu (TSurface *pSurface, TScreen *pScreen, TRect *pDamage)
{
    if (pScreen->nMenu < 0)
    {
        return;
    }
    WidgetDraw (pSurface, pScreen->pTheme, &pScreen->Menu);
    *pDamage = RectUnion (*pDamage, pScreen->Menu.Rect);
}

void ScreenPresent (TSurface *pShadow, TSurface *pOutput, TScreen *pScreen,
                    void (*pRepaint) (TSurface *), bool bPointer, int nX, int nY,
                    unsigned nCursorScale)
{
    TRect Damage = GfxCursorHide (pShadow);

    TRect Painted;
    if (!ScreenPaintDirty (pShadow, pScreen, &Painted))
    {
        pRepaint (pShadow);
        Painted = Rect (0, 0, pShadow->nWidth, pShadow->nHeight);
        ScreenPaintMenu (pShadow, pScreen, &Painted);
    }
    Damage = RectUnion (Damage, Painted);

    if (bPointer)
    {
        const TGfxCursor Shape = ScreenCursorAt (pScreen, nX, nY) == CursorBeam
                                     ? GfxCursorBeam : GfxCursorArrow;
        Damage = RectUnion (Damage, GfxCursorShow (pShadow, nX, nY, nCursorScale, Shape));
    }
    if (Damage.nWidth != 0)
    {
        GfxBlit (pOutput, pShadow, Damage);
    }
}

bool ScreenMenuOpen (const TScreen *pScreen)
{
    return pScreen->nMenu >= 0;
}

bool ScreenPaintDirty (TSurface *pSurface, TScreen *pScreen, TRect *pDamage)
{
    *pDamage = Rect (0, 0, 0, 0);
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
        const TRect Around = RectInset (p->Rect, -(int) nReach, -(int) nReach);
        GfxFill (pSurface, Around, pScreen->Background);
        WidgetDraw (pSurface, pScreen->pTheme, p);
        *pDamage = RectUnion (*pDamage, Around);
    }
    pScreen->nDirtyCount = 0;
    ScreenPaintMenu (pSurface, pScreen, pDamage);   // in front, by definition
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
    pScreen->bCaret      = true;
    pScreen->nMenu       = -1;
    pScreen->nMenuOpenX  = 0;
    pScreen->nMenuOpenY  = 0;
    pScreen->bMenuHeld   = false;
    WidgetClear (&pScreen->Menu);
    pScreen->Bounds      = Rect (0, 0, 0, 0);
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
            pWidgets[i].nState |= StateCaret;
            return;
        }
    }
    SetFocus (pScreen, NextFocus (pScreen, -1, +1));
}

bool ScreenBlinkCaret (TScreen *pScreen)
{
    if (pScreen->nFocus < 0 || pScreen->nMenu >= 0)
    {
        return false;
    }
    TWidget *p = &pScreen->pWidgets[pScreen->nFocus];
    if (p->Type != WidgetField || p->pEdit == 0)
    {
        return false;                   // nothing that carries a caret
    }
    pScreen->bCaret = !pScreen->bCaret;
    if (pScreen->bCaret)
    {
        p->nState |= StateCaret;
    }
    else
    {
        p->nState &= ~(unsigned) StateCaret;
    }
    ScreenTouch (pScreen, pScreen->nFocus);
    return true;
}

TCursorShape ScreenCursorAt (const TScreen *pScreen, int nX, int nY)
{
    // A menu is in front, and nothing under it is being pointed at.
    if (pScreen->nMenu >= 0)
    {
        return RectContains (pScreen->Menu.Rect, nX, nY) ? CursorArrow : CursorArrow;
    }
    const int nHit = WidgetHit (pScreen->pWidgets, pScreen->nCount, nX, nY);
    if (nHit >= 0
        && pScreen->pWidgets[nHit].Type == WidgetField
        && pScreen->pWidgets[nHit].pEdit != 0)
    {
        return CursorBeam;
    }
    return CursorArrow;
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
static bool ListMoveIn (TWidget *p, const TTheme *pTheme, int nStep)
{
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
    WidgetListReveal (p, pTheme);
    return true;
}

static bool ListMove (TScreen *pScreen, int nList, int nStep)
{
    if (!ListMoveIn (&pScreen->pWidgets[nList], pScreen->pTheme, nStep))
    {
        return false;
    }
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
/*
 *  The pop-up's menu
 *
 *  It opens over the control with the chosen item on top of it, the way one
 *  always has: what is already selected stays under the hand, so choosing it
 *  again costs no movement at all. Clamped to the screen, because a pop-up near
 *  the bottom would otherwise open past it.
 */
static void MenuOpen (TScreen *pScreen, int nPopup)
{
    TWidget *p = &pScreen->pWidgets[nPopup];
    if (p->pItems == 0 || p->nItems == 0)
    {
        return;
    }
    const unsigned nRow = pScreen->pTheme->M.nMenuRow;
    const unsigned nHeight = p->nItems * nRow + 2;
    const int nChosen = p->nChoice < 0 ? 0 : p->nChoice;

    int nY = p->Rect.nY - 1 - (int) (nChosen * nRow);
    if (nY + (int) nHeight > pScreen->Bounds.nY + (int) pScreen->Bounds.nHeight)
    {
        nY = pScreen->Bounds.nY + (int) pScreen->Bounds.nHeight - (int) nHeight;
    }
    if (nY < pScreen->Bounds.nY)
    {
        nY = pScreen->Bounds.nY;
    }

    WidgetClear (&pScreen->Menu);
    pScreen->Menu.Type    = WidgetMenu;
    pScreen->Menu.Rect    = Rect (p->Rect.nX, nY, p->Rect.nWidth, nHeight);
    pScreen->Menu.pItems  = p->pItems;
    pScreen->Menu.nItems  = p->nItems;
    pScreen->Menu.nChoice = nChosen;
    pScreen->nMenu = nPopup;
    pScreen->nMenuOpenX = pScreen->nX;
    pScreen->nMenuOpenY = pScreen->nY;
    pScreen->bMenuHeld  = true;
}

static void MenuClose (TScreen *pScreen)
{
    pScreen->nMenu = -1;
    // Everything under it has to come back, and only the screen as a whole
    // knows what was there — a menu is the one thing here that overlaps.
    pScreen->bDirtyAll = true;
}

// Answers what the loop should report. A menu takes every event while it is
// open: that is what modal means, and it is the whole reason a pop-up can be
// read without the rest of the screen answering underneath it.
static TScreenReply HandleMenu (TScreen *pScreen, const TEvent *pEvent)
{
    const int nPopup = pScreen->nMenu;

    if (pEvent->Type == EventKeyDown)
    {
        switch (pEvent->nKey)
        {
        case OkKeyUp:
        case OkKeyDown:
            return Reply (ListMoveIn (&pScreen->Menu, pScreen->pTheme,
                                      pEvent->nKey == OkKeyDown ? +1 : -1)
                              ? ScreenChanged : ScreenIdle, -1);

        case OkKeyEscape:
            MenuClose (pScreen);
            return Reply (ScreenChanged, -1);

        case OkKeySpace:
        case OkKeyReturn:
            pScreen->pWidgets[nPopup].nChoice = pScreen->Menu.nChoice;
            MenuClose (pScreen);
            return Reply (ScreenActivated, nPopup);

        default:
            return Reply (ScreenIdle, -1);
        }
    }

    if (pEvent->Type == EventMouseMove)
    {
        pScreen->nX = pEvent->nX;
        pScreen->nY = pEvent->nY;
        const int nAt = WidgetListItemAt (&pScreen->Menu, pScreen->pTheme,
                                          pEvent->nX, pEvent->nY);
        if (nAt < 0 || nAt == pScreen->Menu.nChoice
            || (pScreen->Menu.pItems[nAt].nState & StateDisabled))
        {
            return Reply (ScreenIdle, -1);
        }
        pScreen->Menu.nChoice = nAt;
        return Reply (ScreenChanged, -1);
    }

    if (pEvent->Type == EventMouseDown || pEvent->Type == EventMouseUp)
    {
        pScreen->nX = pEvent->nX;
        pScreen->nY = pEvent->nY;
        if (!RectContains (pScreen->Menu.Rect, pEvent->nX, pEvent->nY))
        {
            // Outside is how a menu is dismissed, and a press outside is enough
            // — waiting for the release leaves it standing under the hand.
            if (pEvent->Type == EventMouseDown)
            {
                MenuClose (pScreen);
                return Reply (ScreenChanged, -1);
            }
            // A release outside while the opening click is still being let go
            // is not a dismissal: the hand simply came off the pop-up.
            pScreen->bMenuHeld = false;
            return Reply (ScreenIdle, -1);
        }
        if (pEvent->Type == EventMouseUp)
        {
            // The release that ends the opening click leaves the menu standing,
            // unless the hand actually travelled — which is the difference
            // between clicking a pop-up open and pulling down through it.
            if (pScreen->bMenuHeld)
            {
                pScreen->bMenuHeld = false;
                const int dx = pEvent->nX - pScreen->nMenuOpenX;
                const int dy = pEvent->nY - pScreen->nMenuOpenY;
                const int nSlop = (int) pScreen->pTheme->M.nGap;
                if ((dx < 0 ? -dx : dx) <= nSlop && (dy < 0 ? -dy : dy) <= nSlop)
                {
                    return Reply (ScreenIdle, -1);
                }
            }
            const int nAt = WidgetListItemAt (&pScreen->Menu, pScreen->pTheme,
                                              pEvent->nX, pEvent->nY);
            if (nAt < 0 || (pScreen->Menu.pItems[nAt].nState & StateDisabled))
            {
                return Reply (ScreenIdle, -1);
            }
            pScreen->pWidgets[nPopup].nChoice = nAt;
            MenuClose (pScreen);
            return Reply (ScreenActivated, nPopup);
        }
        return Reply (ScreenIdle, -1);
    }
    return Reply (ScreenIdle, -1);
}

static TScreenReply HandleField (TScreen *pScreen, TWidget *p, const TEvent *pEvent)
{
    const bool bExtend = (pEvent->nModifiers & ModShift) != 0;

    // Command and a letter: the four every Macintosh has answered to since
    // 1984, and the reason the bridge translates the key with the Command bit
    // taken out — Circle's map answers nothing at all while it is held.
    if (pEvent->nModifiers & ModCommand)
    {
        const unsigned c = pEvent->nChar >= 'A' && pEvent->nChar <= 'Z'
                               ? pEvent->nChar + 32 : pEvent->nChar;
        switch (c)
        {
        case 'a':   WidgetFieldSelectAll (p);   break;
        case 'c':   WidgetFieldCopy (p);        break;
        case 'x':   WidgetFieldCut (p);         break;
        case 'v':   WidgetFieldPaste (p);       break;
        default:    return Reply (ScreenIdle, -1);
        }
        ScreenTouch (pScreen, pScreen->nFocus);
        return Reply (ScreenChanged, -1);
    }

    bool bMine = true;
    switch (pEvent->nKey)
    {
    case OkKeyBackspace:  WidgetFieldBackspace (p);          break;
    case OkKeyDelete:     WidgetFieldDelete (p);             break;
    case OkKeyLeft:       WidgetFieldCaret (p, -1, bExtend); break;
    case OkKeyRight:      WidgetFieldCaret (p, +1, bExtend); break;
    case OkKeyHome:       WidgetFieldHome (p, bExtend);      break;
    case OkKeyEnd:        WidgetFieldEnd (p, bExtend);       break;
    default:
        // Anything that produced a character. Tab and Return produce none, so
        // they fall through to the screen and keep meaning what they mean
        // everywhere; Control combinations are not text either.
        bMine = pEvent->nChar != 0 && !(pEvent->nModifiers & ModControl);
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
        if (p != 0 && p->Type == WidgetPopup && p->pItems != 0)
        {
            MenuOpen (pScreen, nFocused);
            return Reply (ScreenChanged, -1);
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
    if (pScreen->nMenu >= 0)
    {
        return HandleMenu (pScreen, pEvent);
    }

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
            // Dragging inside a field selects, which is the only way anyone
            // has ever selected text with a mouse.
            if (pScreen->pWidgets[pScreen->nPressed].Type == WidgetField
                && pScreen->pWidgets[pScreen->nPressed].pEdit != 0)
            {
                WidgetFieldClick (&pScreen->pWidgets[pScreen->nPressed], pScreen->pTheme,
                                  pEvent->nX, true);
                ScreenTouch (pScreen, pScreen->nPressed);
                return Reply (ScreenChanged, -1);
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
                // Clicking nothing means aiming at nothing: the focus leaves.
                // A field that keeps its caret because the click landed on the
                // background is a field one cannot get out of with the mouse,
                // and that is where the hand goes first.
                if (pScreen->nFocus < 0)
                {
                    return Reply (ScreenIdle, -1);
                }
                SetFocus (pScreen, -1);
                return Reply (ScreenChanged, -1);
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
            // A click on a pop-up opens its menu, which then has every event
            // until it is done with.
            if (pScreen->pWidgets[nHit].Type == WidgetPopup
                && pScreen->pWidgets[nHit].pItems != 0)
            {
                MenuOpen (pScreen, nHit);
                pScreen->nPressed = -1;
                return Reply (ScreenChanged, -1);
            }
            // A click in a field puts the caret where it landed. Without it the
            // only way to reach the middle of a name is to walk there with the
            // arrows, which nobody does.
            if (pScreen->pWidgets[nHit].Type == WidgetField)
            {
                WidgetFieldClick (&pScreen->pWidgets[nHit], pScreen->pTheme, pEvent->nX,
                                  (pEvent->nModifiers & ModShift) != 0);
                ScreenTouch (pScreen, nHit);
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
