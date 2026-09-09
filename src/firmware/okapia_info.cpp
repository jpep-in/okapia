/*
 * okapia_info.cpp — see okapia_info.h.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_info.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 2 * (INFO_MAX + INFO_CREDITS) + 8;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;
static int     s_nBack = -1;

void InfoAdd (TInfo *pInfo, const char *pLabel, const char *pValue)
{
    if (pInfo->nCount >= INFO_MAX)
    {
        return;
    }
    TInfoLine *p = &pInfo->Lines[pInfo->nCount++];
    StrAppend (p->Label, INFO_LABEL, 0, pLabel);
    StrAppend (p->Value, INFO_VALUE, 0, pValue);
}

void InfoCredit (TInfo *pInfo, const char *pWhat, const char *pWho)
{
    if (pInfo->nCredits >= INFO_CREDITS)
    {
        return;
    }
    TInfoLine *p = &pInfo->Credits[pInfo->nCredits++];
    StrAppend (p->Label, INFO_LABEL, 0, pWhat);
    StrAppend (p->Value, INFO_VALUE, 0, pWho);
}

unsigned InfoWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

TRect InfoDialog (void)
{
    return s_Page.Dialog;
}

bool InfoIsBack (int nIndex)
{
    return nIndex == s_nBack;
}

// One row: its label on the left, its value beside it. Nothing here can be
// operated, so nothing reserves the room a focus ring would want — this is a
// page one reads.
static void Line (TPage *pPage, unsigned nLabel, const TInfoLine *pLine)
{
    const TTheme *pTheme = &pPage->Theme;
    TRow R;
    RowBegin (&R, pTheme, LayoutRow (&pPage->Layout, pTheme->M.nLineHeight, StateNormal));
    PageAdd (pPage, WidgetLabel, RowNext (&R, nLabel, StateNormal), pLine->Label,
             StateDisabled);
    PageAdd (pPage, WidgetLabel, RowRest (&R, StateNormal), pLine->Value, StateNormal);
    LayoutRowGap (&pPage->Layout);
}

void InfoDraw (TSurface *pSurface, TInfo *pInfo)
{
    PageBegin (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, PAGE_WIDTH, PAGE_HEIGHT,
               Str (StrInformation));
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    PageFooter (&s_Page);
    // No focus on it, as in an alert: a page with one button has nothing to
    // navigate, and the ring that says "the keyboard is here" then sits on top
    // of the ring that says "Return does this" and repeats it.
    s_nBack = PageLast (&s_Page, Str (StrBack), StateDefault | StateNoFocus);

    // One label column for both sections, measured over both: two columns of
    // two widths on one page read as two tables that happen to be stacked.
    // Never chosen, either — these labels are translated, and French runs
    // longer than English almost everywhere.
    unsigned nLabel = 0;
    for (unsigned i = 0; i < pInfo->nCount + pInfo->nCredits; i++)
    {
        const TInfoLine *p = i < pInfo->nCount ? &pInfo->Lines[i]
                                               : &pInfo->Credits[i - pInfo->nCount];
        const unsigned n = GfxTextWidth (pTheme->pBodyBoldFont, p->Label);
        if (n > nLabel)
        {
            nLabel = n;
        }
    }
    nLabel += pTheme->M.nGap;

    for (unsigned i = 0; i < pInfo->nCount; i++)
    {
        Line (&s_Page, nLabel, &pInfo->Lines[i]);
    }

    if (pInfo->nCredits != 0)
    {
        LayoutSectionGap (&Layout);
        PageAdd (&s_Page, WidgetSeparator, LayoutTop (&Layout, 1), 0, StateNormal);
        LayoutRowGap (&Layout);
        PageAdd (&s_Page, WidgetLabel, LayoutTop (&Layout, pTheme->M.nLineHeight),
                 Str (StrInfoCredits), StateNormal);
        LayoutRowGap (&Layout);
        for (unsigned i = 0; i < pInfo->nCredits; i++)
        {
            Line (&s_Page, nLabel, &pInfo->Credits[i]);
        }
    }

    InfoRepaint (pSurface);
}

void InfoRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}
