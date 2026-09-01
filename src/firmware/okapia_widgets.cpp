/*
 * okapia_widgets.cpp — components: geometry, state, hit testing. Never pixels.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_widgets.h"

void WidgetDraw (TSurface *pSurface, const TTheme *pTheme, const TWidget *pWidget)
{
    switch (pWidget->Type)
    {
    case WidgetLabel:
        pTheme->DrawLabel (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetTitle:
        pTheme->DrawTitle (pSurface, pWidget->Rect, pWidget->pText, pTheme);
        break;

    case WidgetButton:
        pTheme->DrawButton (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetIconButton:
        pTheme->DrawIconButton (pSurface, pWidget->Rect, pWidget->Paint, pWidget->nState,
                                pTheme);
        break;

    case WidgetCheckbox:
        pTheme->DrawCheckbox (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetRadio:
        pTheme->DrawRadio (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetListFrame:
        pTheme->DrawListFrame (pSurface, pWidget->Rect, pTheme);
        break;

    case WidgetListRow:
        // Background from the theme, contents from the screen, then the theme
        // again to pick the row out. Drawing the contents in ordinary ink and
        // inverting afterwards is what lets one list serve the systems, the
        // settings and the catalogue of phase 17 without being written three
        // times — whatever a row contains is selected by the same rule.
        pTheme->DrawListRow (pSurface, pWidget->Rect, pWidget->nState, pTheme);
        {
            int nInk = pWidget->Rect.nX + (int) pTheme->M.nGap;
            if (pWidget->pIcon != 0)
            {
                const int nIconY = pWidget->Rect.nY
                                 + ((int) pWidget->Rect.nHeight
                                    - (int) (pWidget->pIcon->nHeight * pTheme->nIconScale)) / 2;
                GfxImage (pSurface, pWidget->pIcon, nInk, nIconY, ColorBlack,
                          pTheme->nIconScale);
                nInk += (int) (pWidget->pIcon->nWidth * pTheme->nIconScale)
                      + (int) pTheme->M.nGap;
            }
            if (pWidget->pText == 0)
            {
                break;
            }
            const TRect Text = Rect (nInk, pWidget->Rect.nY,
                                     (unsigned) (pWidget->Rect.nX + (int) pWidget->Rect.nWidth
                                                 - (int) pTheme->M.nGap - nInk),
                                     pWidget->Rect.nHeight);
            const int nY = Text.nY + ((int) Text.nHeight - (int) pTheme->pBodyFont->nHeight) / 2;
            GfxText (pSurface, pTheme->pBodyFont, Text.nX, nY, pWidget->pText,
                     ColorBlack);
        }
        if (pWidget->nState & StateSelected)
        {
            pTheme->DrawSelection (pSurface, pWidget->Rect, pTheme);
        }
        break;

    case WidgetScrollbar:
        pTheme->DrawScrollbar (pSurface, pWidget->Rect, pWidget->nValue, pWidget->nSpan, pTheme);
        break;

    case WidgetPopup:
        pTheme->DrawPopup (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetField:
        pTheme->DrawField (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetProgress:
        pTheme->DrawProgress (pSurface, pWidget->Rect, pWidget->nValue, pTheme);
        break;

    case WidgetSeparator:
        pTheme->DrawSeparator (pSurface, pWidget->Rect, pTheme);
        break;
    }
}

void WidgetDrawAll (TSurface *pSurface, const TTheme *pTheme, const TWidget *pList,
                    unsigned nCount)
{
    for (unsigned i = 0; i < nCount; i++)
    {
        WidgetDraw (pSurface, pTheme, &pList[i]);
    }
}

int WidgetHit (const TWidget *pList, unsigned nCount, int nX, int nY)
{
    // Backwards, so the last drawn is the first hit: the array order is the
    // stacking order, and there is no other one.
    for (int i = (int) nCount - 1; i >= 0; i--)
    {
        const TWidget *p = &pList[i];
        if (p->nState & StateDisabled)
        {
            continue;
        }
        switch (p->Type)
        {
        case WidgetLabel:
        case WidgetTitle:
        case WidgetListFrame:
        case WidgetProgress:
        case WidgetSeparator:
            continue;                   // decoration takes no clicks
        default:
            break;
        }
        if (RectContains (p->Rect, nX, nY))
        {
            return i;
        }
    }
    return -1;
}

unsigned WidgetButtonWidth (const TTheme *pTheme, const char *pText, unsigned nState)
{
    const unsigned nText = GfxTextWidth (
        (nState & StateDefault) ? pTheme->pBodyBoldFont : pTheme->pBodyFont,
        pText);
    const unsigned nWidth = nText + 2 * pTheme->M.nButtonPadX;
    return nWidth < pTheme->M.nButtonMinWidth ? pTheme->M.nButtonMinWidth : nWidth;
}
