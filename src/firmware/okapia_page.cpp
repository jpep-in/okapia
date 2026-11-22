/*
 * okapia_page.cpp — see okapia_page.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_page.h"
#include "okapia_icons.h"
#include "okapia_strings.h"

// The design's own units, multiplied by this page's scale. Every screen states
// its rectangles in the 640x480 the theme was drawn against.
static int PageScale (const TPage *pPage, int nValue)
{
    return nValue * (int) pPage->nScale16 / 16;
}

// The tabbed page laid out last, which is the one on the screen: there is one
// window and it shows one page at a time.
static const TPage *s_pChrome;
static unsigned     s_nFixedScale16;

void PageFixScale (unsigned nScale16)
{
    s_nFixedScale16 = nScale16;
}

static bool         s_bRestart;
static bool         s_bFocusTabs;
static bool         s_bCanStart = true;
static bool         s_bBootMenu;
static bool         s_bFocusBootMenu;
static TListItem    s_TabItems[PageTabCount];
static TPageTab     s_TabOf[PageTabCount];  // strip index to page
static unsigned     s_nTabsShown = PageTabCount;
static bool         s_bTabHidden[PageTabCount];

void PageChromeHideTab (TPageTab Tab, bool bHidden)
{
    if (Tab < PageTabCount)
    {
        s_bTabHidden[Tab] = bHidden;
    }
}

void PageBegin (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                unsigned nWide, unsigned nHigh, const char *pTitle)
{
    pPage->bAlert   = false;
    pPage->nTabs    = -1;
    pPage->nPower   = -1;
    pPage->nPram    = -1;
    pPage->nWorkspace = -1;
    pPage->nStart   = -1;
    pPage->nNote    = -1;
    pPage->nBootMenu = -1;
    s_pChrome       = 0;
    pPage->pWidgets = pWidgets;
    pPage->nMax     = nMax;
    pPage->nCount   = 0;
    pPage->nScale16 = s_nFixedScale16 != 0 ? s_nFixedScale16
                                           : ThemeScaleFor (pSurface->nWidth, pSurface->nHeight);
    ThemeMake (pPage->nScale16, &pPage->Theme);

    if (nWide == 0)
    {
        pPage->Dialog = Rect (0, 0, pSurface->nWidth, pSurface->nHeight);
    }
    else
    {
        const unsigned nDW = (unsigned) PageScale (pPage, (int) nWide);
        const unsigned nDH = (unsigned) PageScale (pPage, (int) nHigh);
        pPage->Dialog = Rect ((int) (pSurface->nWidth  - nDW) / 2,
                              (int) (pSurface->nHeight - nDH) / 2, nDW, nDH);
    }

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
    // on both sides — the band itself is only the button's own height. Focused
    // as well: Start is where the keyboard begins, and its grey ring sits
    // outside the black one.
    const unsigned nReach = ThemeReach (pTheme, StateDefault | StateFocused);
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

static const char *s_pBootMenuHint;
static bool        s_bWorkspaceOffered;

void PageChromeWorkspace (bool bOffered)
{
    s_bWorkspaceOffered = bOffered;
}

void PageChromeBootMenuHint (const char *pHint)
{
    s_pBootMenuHint = pHint;
}

void PageChromeBootMenu (bool bOn, bool bFocus)
{
    s_bBootMenu      = bOn;
    s_bFocusBootMenu = bFocus;
}

void PageGroupBegin (TPage *pPage, TPageGroup *pGroup, const char *pTitle)
{
    const TTheme *pTheme = &pPage->Theme;
    TLayout *pLayout = &pPage->Layout;
    pGroup->nTop   = pLayout->Free.nY;
    pGroup->nX     = pLayout->Free.nX;
    pGroup->nWidth = pLayout->Free.nWidth;
    pGroup->nIndex = (int) pPage->nCount;
    PageAdd (pPage, WidgetGroup, Rect (pGroup->nX, pGroup->nTop, pGroup->nWidth, 0),
             pTitle, StateNormal);
    // The name's line, then the air under the frame's top; inside, a gap on
    // either side.
    LayoutSkip (pLayout, pTheme->M.nLineHeight);
    pLayout->Free.nX     += (int) pTheme->M.nGap;
    pLayout->Free.nWidth -= 2 * pTheme->M.nGap;
}

void PageGroupEnd (TPage *pPage, TPageGroup *pGroup)
{
    const TTheme *pTheme = &pPage->Theme;
    TLayout *pLayout = &pPage->Layout;
    LayoutSkip (pLayout, pTheme->M.nGap / 2 + pTheme->M.nStroke);
    pLayout->Free.nX     = pGroup->nX;
    pLayout->Free.nWidth = pGroup->nWidth;
    pPage->pWidgets[pGroup->nIndex].Rect.nHeight =
        (unsigned) (pLayout->Free.nY - pGroup->nTop);
}

void PageChromeSet (bool bRestart, bool bFocusTabs, bool bCanStart)
{
    s_bRestart   = bRestart;
    s_bFocusTabs = bFocusTabs;
    s_bCanStart  = bCanStart;
}

void PageBeginTabbed (TPage *pPage, TSurface *pSurface, TWidget *pWidgets, unsigned nMax,
                      TPageTab Tab)
{
    PageBegin (pPage, pSurface, pWidgets, nMax, PAGE_WIDTH, PAGE_HEIGHT, 0);
    const TTheme *pTheme = &pPage->Theme;

    // The strip takes the title's place: the tab in front *is* the title, and
    // a window naming its page twice reads as two windows.
    static const TStringId Names[PageTabCount] =
    {
        StrTabSystem, StrTabSettings, StrTabNetwork, StrTabDisplay, StrTabInformation,
        StrTabJournal
    };
    // The strip shows the tabs not left out; its index is mapped back to the
    // page it names (s_TabOf), so a hidden one leaves no gap.
    s_nTabsShown = 0;
    int nShown = 0;
    for (unsigned i = 0; i < PageTabCount; i++)
    {
        if (s_bTabHidden[i])
        {
            continue;
        }
        if (i == (unsigned) Tab)
        {
            nShown = (int) s_nTabsShown;
        }
        TListItem &Item = s_TabItems[s_nTabsShown];
        Item.pText  = Str (Names[i]);
        Item.pIcon  = 0;
        Item.nState = StateNormal;
        s_TabOf[s_nTabsShown++] = (TPageTab) i;
    }
    pPage->nTabs = (int) pPage->nCount;
    TWidget *pTabs = PageAdd (pPage, WidgetTabs,
                              LayoutTop (&pPage->Layout, ThemeTabsHeight (pTheme)), 0,
                              s_bFocusTabs ? StateFocused : StateNormal);
    pTabs->pItems  = s_TabItems;
    pTabs->nItems  = s_nTabsShown;
    pTabs->nChoice = nShown;
    LayoutSectionGap (&pPage->Layout);

    // The machine's buttons, the same on every page. Start is where the
    // keyboard begins unless the page was turned from the strip: Start is what
    // one came to press, and Return acts on what the keyboard holds.
    PageFooter (pPage);
    pPage->nPram  = PageIconButton (pPage, OkapiaPaintPram,  StateNormal);
    pPage->nPower = PageIconButton (pPage, OkapiaPaintPower, StateNormal);
    if (s_bWorkspaceOffered)
    {
        pPage->nWorkspace = PageIconButton (pPage, OkapiaPaintLogo, StateNormal);
    }
    // Laid out with the label it will actually wear: a button sized for "Save
    // and restart" and drawn saying "Start" is a button with a hole in it, so a
    // change of either lays the page out again.
    const bool bStartFocus = !s_bFocusTabs && !s_bFocusBootMenu && s_bCanStart;
    pPage->nStart = PageLast (pPage, s_bRestart ? Str (StrSaveRestart) : Str (StrStart),
                              (bStartFocus ? StateDefault | StateFocused : StateDefault)
                              | (s_bCanStart ? StateNormal : StateDisabled));

    // Where the grey line under the tick box goes, set below with the box.
    TRect Hint = Rect (0, 0, 0, 0);
    int nCheckTop = 0;

    // Beside Start, because it is about starting: whether this window comes
    // back next time. A habit of the person and not a setting of the
    // Macintosh, so it is the window's and on every page.
    {
        const char *pLabel = Str (StrBootMenu);
        const unsigned nWide = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, pLabel);
        TRect Box = RowLast (&pPage->Foot, nWide, StateFocused);
        // Unticked, the footer has a second line for the way back to the menu,
        // and the tick box moves up to make room for it: the footer's height is
        // the buttons' with the room their rings need, and the two lines fit
        // in it, so the pages above lose nothing.
        const unsigned nRingReach = ThemeReach (pTheme, StateDefault | StateFocused);
        const int nFootTop = Box.nY - (int) nRingReach;
        const int nOneLine = Box.nY + ((int) pTheme->M.nButtonHeight - (int) pTheme->M.nCheckSize) / 2;
        const int nStacked = nFootTop + (int) ThemeReach (pTheme, StateFocused);
        nCheckTop = s_bBootMenu ? nOneLine : nStacked;
        Hint = Rect (Box.nX + (int) Box.nWidth, nCheckTop + (int) pTheme->M.nCheckSize,
                     0, pTheme->M.nLineHeight);
        Box = Rect (Box.nX, nCheckTop, Box.nWidth, pTheme->M.nCheckSize);
        pPage->nBootMenu = (int) pPage->nCount;
        PageAdd (pPage, WidgetCheckbox, Box, pLabel,
                 (s_bBootMenu ? StateChecked : StateNormal)
                 | (s_bFocusBootMenu && !s_bFocusTabs ? StateFocused : 0));
    }
    if (s_bRestart)
    {
        pPage->nNote = PageNote (pPage, Str (StrRestartRequired), StateNormal);
        if (!s_bBootMenu)
        {
            // On the tick box's line, and not on the buttons': the grey line
            // under the box runs left under the note, and the two must not
            // share one.
            TRect &Note = pPage->pWidgets[pPage->nNote].Rect;
            Note.nY = nCheckTop + (int) pTheme->M.nCheckSize - (int) pTheme->M.nLineHeight;
        }
    }
    if (!s_bBootMenu)
    {
        // Under the tick box, ending where it ends, so that it reads as its
        // own note; it runs to the left, over the free part of the footer.
        const char *pText = s_pBootMenuHint != 0 ? s_pBootMenuHint : Str (StrBootMenuHint);
        const unsigned nText = GfxTextWidth (pTheme->pBodyFont, pText);
        Hint = Rect (Hint.nX - (int) nText, Hint.nY, nText, Hint.nHeight);
        PageAdd (pPage, WidgetLabel, Hint, pText, StateDisabled);
    }

    s_pChrome = pPage;
}

bool PageChromeRestart (void)
{
    return s_bRestart;
}

int PageChromeAnswer (int nIndex)
{
    const TPage *p = s_pChrome;
    if (p == 0 || nIndex < 0)
    {
        return PageAnswerNone;
    }
    if (nIndex == p->nStart)  return PageAnswerStart;
    if (nIndex == p->nPower)  return PageAnswerShutDown;
    if (nIndex == p->nPram)   return PageAnswerForgetPram;
    if (nIndex == p->nWorkspace) return PageAnswerWorkspace;
    if (nIndex == p->nBootMenu) return PageAnswerBootMenu;
    if (nIndex == p->nTabs)
    {
        const int nShown = p->pWidgets[p->nTabs].nChoice;
        return nShown >= 0 && nShown < (int) s_nTabsShown ? PageAnswerTab + (int) s_TabOf[nShown]
                                                          : PageAnswerNone;
    }
    return PageAnswerNone;
}
