/*
 * okapia_page.cpp — see okapia_page.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_page.h"

// The design's own units, multiplied by this page's scale. Every screen states
// its rectangles in the 640x480 the theme was drawn against.
static int PageScale (const TPage *pPage, int nValue)
{
    return nValue * (int) pPage->nScale16 / 16;
}

void PageBegin (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                unsigned nWide, unsigned nHigh, const char *pTitle)
{
    pPage->bAlert   = false;
    pPage->pWidgets = pWidgets;
    pPage->nMax     = nMax;
    pPage->nCount   = 0;
    pPage->nScale16 = ThemeScaleFor (pSurface->nWidth, pSurface->nHeight);
    ThemeMake (pPage->nScale16, &pPage->Theme);

    const unsigned nDW = (unsigned) PageScale (pPage, (int) nWide);
    const unsigned nDH = (unsigned) PageScale (pPage, (int) nHigh);
    pPage->Dialog = Rect ((int) (pSurface->nWidth  - nDW) / 2,
                          (int) (pSurface->nHeight - nDH) / 2, nDW, nDH);

    LayoutBegin (&pPage->Layout, &pPage->Theme, ThemeContent (pPage->Dialog, &pPage->Theme));

    if (pTitle != 0)
    {
        PageAdd (pPage, WidgetTitle,
                 LayoutTop (&pPage->Layout, pPage->Theme.pTitleFont->nHeight),
                 pTitle, StateNormal);
        LayoutRowGap (&pPage->Layout);
        PageAdd (pPage, WidgetSeparator, LayoutTop (&pPage->Layout, 1), 0, StateNormal);
        LayoutSectionGap (&pPage->Layout);
    }

    RowBegin (&pPage->Foot, &pPage->Theme, Rect (0, 0, 0, 0));
}

void PageBeginAlert (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                     unsigned nWide, unsigned nHigh, const char *pTitle)
{
    PageBegin (pPage, pSurface, pWidgets, nMax, nWide, nHigh, pTitle);
    pPage->bAlert = true;
}

TWidget *PageAdd (TPage *pPage, TWidgetType Type, const TRect &rRect,
                  const char *pText, unsigned nState)
{
    // The last slot rather than a new one: a screen that asks for more than it
    // declared has a bug in its own count, and overwriting one component is a
    // great deal easier to see than writing past the array.
    if (pPage->nCount >= pPage->nMax)
    {
        return &pPage->pWidgets[pPage->nMax - 1];
    }
    TWidget *p = &pPage->pWidgets[pPage->nCount++];
    WidgetClear (p);
    p->Type   = Type;
    p->Rect   = rRect;
    p->pText  = pText;
    p->nState = nState;
    return p;
}

void PageFooter (TPage *pPage)
{
    const TTheme *pTheme = &pPage->Theme;

    // Taken off the bottom with the room a default button's double ring needs
    // on both sides — the band itself is only the button's own height.
    const unsigned nReach = ThemeReach (pTheme, StateDefault);
    TRect Band = LayoutBottom (&pPage->Layout, pTheme->M.nButtonHeight + 2 * nReach);
    Band = Rect (Band.nX, Band.nY + (int) nReach, Band.nWidth, pTheme->M.nButtonHeight);
    RowBegin (&pPage->Foot, pTheme, Band);

    LayoutSectionGapBottom (&pPage->Layout);
    PageAdd (pPage, WidgetSeparator, LayoutBottom (&pPage->Layout, 1), 0, StateNormal);
    LayoutSectionGapBottom (&pPage->Layout);
}

// Placed with the room a focus ring needs whatever state it is drawn in: the
// layout runs before the loop and cannot know which control will be focused.
int PageLast (TPage *pPage, const char *pText, unsigned nState)
{
    const unsigned nWidth = WidgetButtonWidth (&pPage->Theme, pText, nState);
    const int nIndex = (int) pPage->nCount;
    PageAdd (pPage, WidgetButton, RowLast (&pPage->Foot, nWidth, nState | StateFocused),
             pText, nState);
    return nIndex;
}

int PageIconButton (TPage *pPage, TIconPainter Paint, unsigned nState)
{
    const unsigned nSize = pPage->Theme.M.nButtonHeight;
    const int nIndex = (int) pPage->nCount;
    PageAdd (pPage, WidgetIconButton, RowNext (&pPage->Foot, nSize, nState | StateFocused),
             0, nState)->Paint = Paint;
    return nIndex;
}

int PageNote (TPage *pPage, const char *pText, unsigned nState)
{
    const TTheme *pTheme = &pPage->Theme;
    TRect Rest = RowRest (&pPage->Foot, StateNormal);
    // On the buttons' line rather than at the top of the band: a button is
    // taller than a line of text.
    Rest = Rect (Rest.nX,
                 Rest.nY + ((int) pTheme->M.nButtonHeight - (int) pTheme->M.nLineHeight) / 2,
                 Rest.nWidth, pTheme->M.nLineHeight);
    const int nIndex = (int) pPage->nCount;
    PageAdd (pPage, WidgetLabel, Rest, pText, nState);
    return nIndex;
}

void PagePaint (TSurface *pSurface, const TPage *pPage)
{
    const TTheme *pTheme = &pPage->Theme;
    pTheme->DrawDesktop (pSurface, pTheme);
    if (pPage->bAlert)
    {
        pTheme->DrawAlert (pSurface, pPage->Dialog, pTheme);
    }
    else
    {
        pTheme->DrawDialog (pSurface, pPage->Dialog, pTheme);
    }
    WidgetDrawAll (pSurface, pTheme, pPage->pWidgets, pPage->nCount);
}
