/*
 * okapia_welcome.cpp — the welcome page: what Okapia is, and its Okapia Workspace.
 *
 * The logo beside two sentences that say what Okapia needs, then the three
 * steps, each a frame with what is there and the button that adds to it. A
 * step that cannot be taken yet is grey rather than absent: without a
 * workspace there is nowhere to put a ROM, and the page says so by its look.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_welcome.h"
#include "okapia_icons.h"
#include "okapia_layout.h"
#include "okapia_logo_shape.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 48;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

static int s_nTarget   = -1;
static TListItem s_TargetItems[WELCOME_TARGETS];
static int s_nPrepare  = -1;
static int s_nCard     = -1;
static int s_nOpen     = -1;
static int s_nCreate   = -1;
static int s_nChoose[WelcomeRomCount] = { -1, -1 };
static int s_nListen[WelcomeRomCount] = { -1, -1 };
static int s_nRomLine[WelcomeRomCount] = { -1, -1 };
static int s_nRomNote[WelcomeRomCount] = { -1, -1 };
static int s_nAddSys   = -1;
static int s_nContinue = -1;
static int s_nQuit     = -1;
static int s_nStatus   = -1;

// The lines each step shows, set by WelcomeSync.
static int s_nWorkspaceLine = -1;
static int s_nWorkspaceNote = -1;
static int s_nSysLines[WELCOME_LINES + 1];
// The disks the page names, one more line saying how many others there are:
// two fit under the ROMs at every scale, 640x480 included; a page that scrolls
// (a desktop's window) names them all, up to WELCOME_LINES.
static unsigned s_nDisksShown = 2;
static unsigned s_nContentTop, s_nContentBottom, s_nSurfaceHeight;
static char s_More[64];

unsigned WelcomeWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

void WelcomeRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

// One step: a frame, its lines, and its buttons on the last line's right.
// A grey paragraph across the group, wrapped: what a step accepts, said once.
static void Hint (const char *pText)
{
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;
    const TRect Whole = LayoutTop (&Layout, 0);
    const unsigned nHigh = GfxTextWrapHeight (pTheme->pBodyFont, Whole.nWidth, pText, pTheme->M.nLineHeight);
    LayoutRowGap (&Layout);
    PageAdd (&s_Page, WidgetParagraph, LayoutTop (&Layout, nHigh), pText, StateDisabled);
}

static void Step (TStringId Title, int *pLines, unsigned nLines,
                  const TStringId *pButtons, int *pnButtons, unsigned nButtons, const char *pHint = 0)
{
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;
    TPageGroup Group;
    PageGroupBegin (&s_Page, &Group, Str (Title));
    // Every line at the same pitch, the last one included: its buttons are
    // centred on it and reach into the air around it, which the layout then
    // keeps below (above, the row gap and the line before take it). A taller
    // band for the last line put it visibly further from the one before.
    const unsigned nLine = pTheme->M.nLineHeight;
    const unsigned nButtonH = pTheme->M.nButtonHeight;
    const unsigned nOver = nButtonH > nLine ? (nButtonH - nLine) / 2 : 0;
    if (nLines == 1)
    {
        LayoutSkip (&Layout, nOver + ThemeReach (pTheme, StateFocused));
    }
    for (unsigned i = 0; i < nLines; i++)
    {
        const bool bLast = i + 1 == nLines;
        TRow Band;
        const TRect Line = LayoutRow (&Layout, nLine, StateNormal);
        RowBegin (&Band, pTheme, Rect (Line.nX, Line.nY - (int) nOver, Line.nWidth, nButtonH));
        if (bLast)
        {
            for (unsigned b = nButtons; b-- > 0; )
            {
                const char *pText = Str (pButtons[b]);
                pnButtons[b] = (int) s_Page.nCount;
                PageAdd (&s_Page, WidgetButton,
                         RowLast (&Band, WidgetButtonWidth (pTheme, pText, StateNormal), StateFocused),
                         pText, StateNormal);
            }
        }
        const TRect Rest = RowRest (&Band, StateNormal);
        pLines[i] = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetLabel, Rect (Rest.nX, Line.nY, Rest.nWidth, nLine), "", StateNormal);
        if (!bLast)
        {
            LayoutRowGap (&Layout);
        }
        else
        {
            LayoutSkip (&Layout, nOver + ThemeReach (pTheme, StateFocused));
        }
    }
    if (pHint != 0)
    {
        Hint (pHint);
    }
    PageGroupEnd (&s_Page, &Group);
    LayoutSectionGap (&Layout);
}

// A rule from the frame's inside edge to the other: the header's and the
// footer's, which divide the window rather than a group in it.
static TRect Across (int nY)
{
    const TTheme *pTheme = &s_Page.Theme;
    const int nFrame = (int) (pTheme->M.nDialogBorder + pTheme->M.nDialogGap + pTheme->M.nDialogInner);
    const TRect &D = s_Page.Dialog;
    return Rect (D.nX + nFrame, nY, D.nWidth > (unsigned) (2 * nFrame) ? D.nWidth - (unsigned) (2 * nFrame) : 0, 1);
}

void WelcomeDraw (TSurface *pSurface, TWelcome *pWelcome)
{
    // No title of PageBegin's: the header below carries it beside the logo.
    PageBegin (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, pWelcome->bFill ? 0 : PAGE_WIDTH, PAGE_HEIGHT, 0);
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;
    s_nDisksShown = pWelcome->bFill ? WELCOME_LINES : 2;
    s_nSurfaceHeight = pSurface->nHeight;

    // The footer's rule across the whole window too: the installer's bar.
    PageFooter (&s_Page);
    {
        TWidget &Rule = s_Widgets[s_Page.nCount - 1];        // PageFooter's last
        Rule.Rect = Across (Rule.Rect.nY);
        s_nContentBottom = (unsigned) Rule.Rect.nY;
    }
    const bool bForBoard = pWelcome->nTarget != WelcomeForDesktop;
    s_nPrepare = -1;
    if (bForBoard)
    {
        // The card's own button, the default: what the page is for, for a Pi.
        s_nPrepare = PageLast (&s_Page, Str (pWelcome->bUpdate ? StrWelcomeUpdateCard : StrWelcomePrepare),
                               StateDefault);
    }
    s_nContinue = PageLast (&s_Page, Str (StrWelcomeContinue), bForBoard ? StateNormal : StateDefault);
    s_nQuit     = PageLast (&s_Page, Str (StrWelcomeQuit), StateNormal);
    s_nStatus   = PageNote (&s_Page, pWelcome->Status, StateDisabled);

    // The header, as the installer's: the logo, and beside it the title over
    // what Okapia is (the sentence fixes the words the steps then use); a rule
    // across the whole window under it. What follows scrolls; this does not.
    {
        const char *pTitle = Str (StrWelcomeTitle);
        const TRect Whole = LayoutTop (&Layout, 0);
        const unsigned nLine = pTheme->M.nLineHeight;
        const unsigned nTitleH = pTheme->pTitleFont->nHeight;
        // The logo's width from its height, which the text's sets: as tall as
        // the title and the sentence, within three to four lines.
        const unsigned nGuessW = (unsigned) ((float) (4 * nLine) * LOGO_CASE_W / LOGO_CASE_H + 0.5f);
        const unsigned nTextW = Whole.nWidth > nGuessW + 2 * pTheme->M.nGap
                              ? Whole.nWidth - nGuessW - 2 * pTheme->M.nGap : Whole.nWidth;
        const unsigned nIntroH = GfxTextWrapHeight (pTheme->pBodyFont, nTextW, Str (StrWelcomeIntro), nLine);
        const unsigned nTextH = nTitleH + pTheme->M.nRowGap + nIntroH;
        unsigned nLogoH = nTextH < 3 * nLine ? 3 * nLine : nTextH > 4 * nLine ? 4 * nLine : nTextH;
        const unsigned nLogoW = (unsigned) ((float) nLogoH * LOGO_CASE_W / LOGO_CASE_H + 0.5f);
        const unsigned nHigh = nTextH > nLogoH ? nTextH : nLogoH;
        TRow Band;
        RowBegin (&Band, pTheme, LayoutTop (&Layout, nHigh));
        const TRect Logo = RowNext (&Band, nGuessW, StateNormal);
        PageAdd (&s_Page, WidgetIcon, Rect (Logo.nX + (int) (nGuessW - nLogoW) / 2,
                                            Logo.nY + (int) (nHigh - nLogoH) / 2, nLogoW, nLogoH),
                 0, StateNormal)->Paint = OkapiaPaintLogo;
        RowSkip (&Band, pTheme->M.nGap);
        const TRect Text = RowRest (&Band, StateNormal);
        const int nTop = Text.nY + (int) (nHigh - nTextH) / 2;
        const unsigned nTitleW = GfxTextWidth (pTheme->pTitleBoldFont, pTitle);
        PageAdd (&s_Page, WidgetTitle,
                 Rect (Text.nX, nTop, nTitleW < Text.nWidth ? nTitleW : Text.nWidth, nTitleH), pTitle,
                 StateNormal);
        PageAdd (&s_Page, WidgetParagraph,
                 Rect (Text.nX, nTop + (int) (nTitleH + pTheme->M.nRowGap), Text.nWidth, nIntroH),
                 Str (StrWelcomeIntro), StateNormal);
    }
    LayoutSectionGap (&Layout);
    {
        const TRect Line = LayoutTop (&Layout, 1);
        PageAdd (&s_Page, WidgetSeparator, Across (Line.nY), 0, StateNormal);
        s_nContentTop = (unsigned) Line.nY + 1;
    }
    LayoutSectionGap (&Layout);

    // Whom it is for, first: everything below follows from it (the
    // installer's "For").
    {
        const unsigned nHigh = pTheme->M.nButtonHeight;
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, nHigh, StateFocused));
        const char *pFor = Str (StrWelcomeFor);
        const TRect Label = RowNext (&Band, GfxTextWidth (pTheme->pBodyFont, pFor), StateNormal);
        PageAdd (&s_Page, WidgetLabel,
                 Rect (Label.nX, Label.nY + (int) (nHigh - pTheme->M.nLineHeight) / 2, Label.nWidth,
                       pTheme->M.nLineHeight),
                 pFor, StateNormal);
        unsigned nWide = 0;
        for (unsigned t = 0; t < pWelcome->nTargets; t++)
        {
            const unsigned w = GfxTextWidth (pTheme->pBodyFont, pWelcome->TargetName[t]);
            nWide = w > nWide ? w : nWide;
        }
        s_nTarget = (int) s_Page.nCount;
        TWidget *pPopup = PageAdd (&s_Page, WidgetPopup,
                                   RowNext (&Band, nWide + 3 * pTheme->M.nButtonHeight / 2, StateFocused),
                                   nullptr, StateNormal);
        pPopup->pItems = s_TargetItems;
    }
    LayoutSectionGap (&Layout);

    // The path beside the ways to choose one, then what its volume is, across
    // the whole width: a card's verdict is a sentence. For a board the step is
    // its card, and a folder may stand in for one (the installer's Advanced).
    {
        const bool bBoard = pWelcome->nTarget != WelcomeForDesktop;
        TPageGroup Group;
        PageGroupBegin (&s_Page, &Group, Str (bBoard ? StrWelcomeStepCard : StrWelcomeStepWorkspace));
        const unsigned nHigh = pTheme->M.nButtonHeight;
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, nHigh, StateFocused));
        s_nCard = s_nOpen = s_nCreate = -1;
        static const TStringId DesktopButtons[] = { StrWelcomeCreate, StrWelcomeOpen };
        static const TStringId BoardButtons[]   = { StrWelcomeFolder, StrWelcomeCard };
        int *const pDesktopIndex[] = { &s_nCreate, &s_nOpen };
        int *const pBoardIndex[]   = { &s_nOpen, &s_nCard };
        const TStringId *pButtons = bBoard ? BoardButtons : DesktopButtons;
        int *const *pIndex = bBoard ? pBoardIndex : pDesktopIndex;
        for (unsigned b = 0; b < 2; b++)
        {
            const char *pText = Str (pButtons[b]);
            *pIndex[b] = (int) s_Page.nCount;
            PageAdd (&s_Page, WidgetButton,
                     RowLast (&Band, WidgetButtonWidth (pTheme, pText, StateNormal), StateFocused),
                     pText, StateNormal);
        }
        TRect Text = RowRest (&Band, StateNormal);
        Text = Rect (Text.nX, Text.nY + (int) (nHigh - pTheme->M.nLineHeight) / 2, Text.nWidth,
                     pTheme->M.nLineHeight);
        s_nWorkspaceLine = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetLabel, Text, "", StateNormal);
        s_nWorkspaceNote = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetLabel, LayoutTop (&Layout, pTheme->M.nLineHeight), "", StateDisabled);
        // The buttons copy files in; the person may as well do it.
        Hint (Str (bBoard ? StrWelcomeHintCard : StrWelcomeHintFolder));
        PageGroupEnd (&s_Page, &Group);
        LayoutSectionGap (&Layout);
    }

    // The ROMs, one line an engine: its name in a column of its own, what it
    // is beside it with its two buttons, and under it what it starts.
    {
        TPageGroup Group;
        PageGroupBegin (&s_Page, &Group, Str (StrWelcomeStepRom));
        static const TStringId Engines[WelcomeRomCount] = { StrWelcomeFl68k, StrWelcomeFlPpc };
        const unsigned nEngineW = GfxTextWidth (pTheme->pBodyBoldFont, Str (StrWelcomeFlPpc));
        for (unsigned e = 0; e < WelcomeRomCount; e++)
        {
            TRow Band;
            const unsigned nHigh = pTheme->M.nButtonHeight;
            RowBegin (&Band, pTheme, LayoutRow (&Layout, nHigh, StateFocused));
            const TRect Name = RowNext (&Band, nEngineW, StateNormal);
            PageAdd (&s_Page, WidgetLabel,
                     Rect (Name.nX, Name.nY + (int) (nHigh - pTheme->M.nLineHeight) / 2, Name.nWidth,
                           pTheme->M.nLineHeight),
                     Str (Engines[e]), StateStrong);
            const char *pChoose = Str (pWelcome->Rom[e].bThere ? StrWelcomeRomReplace : StrWelcomeRomChoose);
            s_nChoose[e] = (int) s_Page.nCount;
            PageAdd (&s_Page, WidgetButton,
                     RowLast (&Band, WidgetButtonWidth (pTheme, Str (StrWelcomeRomReplace), StateNormal),
                              StateFocused),
                     pChoose, StateNormal);
            // A loudspeaker rather than a word: the chime is a sound.
            s_nListen[e] = (int) s_Page.nCount;
            PageAdd (&s_Page, WidgetIconButton, RowLast (&Band, pTheme->M.nButtonHeight, StateFocused),
                     0, StateNormal)->Paint = OkapiaPaintSpeaker;
            TRect Text = RowRest (&Band, StateNormal);
            Text = Rect (Text.nX, Text.nY + (int) (nHigh - pTheme->M.nLineHeight) / 2, Text.nWidth,
                         pTheme->M.nLineHeight);
            s_nRomLine[e] = (int) s_Page.nCount;
            PageAdd (&s_Page, WidgetLabel, Text, "", StateNormal);

            // Under the text, not under the engine's name.
            const TRect Below = LayoutTop (&Layout, pTheme->M.nLineHeight);
            const int nIndent = Text.nX - Below.nX;
            s_nRomNote[e] = (int) s_Page.nCount;
            PageAdd (&s_Page, WidgetLabel,
                     Rect (Text.nX, Below.nY,
                           (int) Below.nWidth > nIndent ? Below.nWidth - (unsigned) nIndent : 0,
                           Below.nHeight),
                     "", StateDisabled);
            if (e + 1 < WelcomeRomCount)
            {
                LayoutRowGap (&Layout);
            }
        }
        PageGroupEnd (&s_Page, &Group);
        LayoutSectionGap (&Layout);
    }

    const unsigned nSysLines = pWelcome->nSystems == 0 ? 1
                             : (pWelcome->nSystems > s_nDisksShown ? s_nDisksShown + 1
                                                                    : pWelcome->nSystems);
    static const TStringId SysButtons[] = { StrWelcomeAddSystem };
    Step (StrWelcomeStepSystem, s_nSysLines, nSysLines, SysButtons, &s_nAddSys, 1, Str (StrWelcomeHintDisks));
    for (unsigned i = nSysLines; i <= WELCOME_LINES; i++)
    {
        s_nSysLines[i] = -1;
    }

    WelcomeSync (pWelcome);
    WelcomeRepaint (pSurface);
}

unsigned WelcomeSpare (void)
{
    return s_Page.Layout.bOverflow ? 0 : s_Page.Layout.Free.nHeight;
}

void WelcomeBands (unsigned *pnTop, unsigned *pnBottom, unsigned *pnRight)
{
    const TTheme *pTheme = &s_Page.Theme;
    *pnTop = s_nContentTop;
    *pnBottom = s_nSurfaceHeight > s_nContentBottom ? s_nSurfaceHeight - s_nContentBottom : 0;
    *pnRight = pTheme->M.nDialogBorder + pTheme->M.nDialogGap + pTheme->M.nDialogInner;
}

static void Fill (int *pLines, const char (*pText)[WELCOME_LINE], unsigned nCount, TStringId None,
                  char *pMore, unsigned nMore)
{
    for (unsigned i = 0; i <= WELCOME_LINES && pLines[i] >= 0; i++)
    {
        TWidget &W = s_Widgets[pLines[i]];
        if (nCount == 0)
        {
            W.pText = Str (None);
            WidgetSetState (&W, StateDisabled);
        }
        else if (i < s_nDisksShown && i < nCount)
        {
            W.pText = pText[i];
            WidgetSetState (&W, StateNormal);
        }
        else
        {
            // "and 2 more": the folder holds them; the page has no room to list.
            unsigned n = StrAppend (pMore, nMore, 0, "+ ");
            n = StrAppendNumber (pMore, nMore, n, nCount - s_nDisksShown);
            n = StrAppend (pMore, nMore, n, " ");
            StrAppend (pMore, nMore, n, Str (StrWelcomeAndMore));
            W.pText = pMore;
            WidgetSetState (&W, StateDisabled);
        }
    }
}

void WelcomeSync (TWelcome *pWelcome)
{
    // The For menu: what the release names, read again as it arrives.
    for (unsigned t = 0; t < pWelcome->nTargets && t < WELCOME_TARGETS; t++)
    {
        s_TargetItems[t] = {};
        s_TargetItems[t].pText = pWelcome->TargetName[t];
    }
    s_Widgets[s_nTarget].nItems = pWelcome->nTargets;
    s_Widgets[s_nTarget].nChoice = (int) pWelcome->nTarget;
    if (s_nPrepare >= 0)
    {
        s_Widgets[s_nPrepare].pText = Str (pWelcome->bUpdate ? StrWelcomeUpdateCard : StrWelcomePrepare);
        WidgetSetState (&s_Widgets[s_nPrepare],
                        pWelcome->bPrepareEnabled ? StateDefault : StateDefault | StateDisabled);
    }
    const bool bWorkspace = pWelcome->Workspace[0] != '\0';
    TWidget &Path = s_Widgets[s_nWorkspaceLine];
    Path.pText = bWorkspace ? pWelcome->Workspace
               : Str (pWelcome->nTarget != WelcomeForDesktop ? StrWelcomeNoCard : StrWelcomeNoWorkspace);
    WidgetSetState (&Path, bWorkspace ? StateNormal : StateDisabled);
    s_Widgets[s_nWorkspaceNote].pText = pWelcome->WorkspaceNote;
    WidgetSetState (&s_Widgets[s_nWorkspaceNote], StateDisabled);

    for (unsigned e = 0; e < WelcomeRomCount; e++)
    {
        const TWelcomeRom &R = pWelcome->Rom[e];
        s_Widgets[s_nRomLine[e]].pText = R.Line;
        WidgetSetState (&s_Widgets[s_nRomLine[e]], R.bThere ? StateNormal : StateDisabled);
        s_Widgets[s_nRomNote[e]].pText = R.Note;
        WidgetSetState (&s_Widgets[s_nChoose[e]], bWorkspace ? StateNormal : StateDisabled);
        WidgetSetState (&s_Widgets[s_nListen[e]], R.bChime ? StateNormal : StateDisabled);
    }
    Fill (s_nSysLines, pWelcome->System, pWelcome->nSystems, StrWelcomeNoSystem, s_More, sizeof s_More);

    // Nothing to add to before there is somewhere to add it.
    WidgetSetState (&s_Widgets[s_nAddSys], bWorkspace ? StateNormal : StateDisabled);
    const unsigned nContinue = pWelcome->nTarget != WelcomeForDesktop ? StateNormal : StateDefault;
    WidgetSetState (&s_Widgets[s_nContinue], bWorkspace ? nContinue : nContinue | StateDisabled);

    if (pWelcome->Status[0] == '\0')
    {
        StrAppend (pWelcome->Status, sizeof pWelcome->Status, 0,
                   Str (!bWorkspace ? (pWelcome->nTarget != WelcomeForDesktop ? StrWelcomeNeedCard : StrWelcomeNeed)
                        : pWelcome->bNothingStarts ? StrWelcomeNeedMatchingRom : StrWelcomeReady));
    }
    s_Widgets[s_nStatus].pText = pWelcome->Status;
}

TWelcomeAction WelcomeOperate (TWelcome *pWelcome, int nIndex)
{
    if (nIndex == s_nTarget)
    {
        const int nChoice = s_Widgets[s_nTarget].nChoice;
        if (nChoice >= 0 && nChoice < (int) pWelcome->nTargets && (unsigned) nChoice != pWelcome->nTarget)
        {
            pWelcome->nTarget = (unsigned) nChoice;
            return WelcomeTarget;
        }
        return WelcomeNothing;
    }
    if (nIndex == s_nCard)     return WelcomeCard;
    if (nIndex == s_nPrepare && s_nPrepare >= 0) return WelcomePrepare;
    if (nIndex == s_nOpen)     return WelcomeOpen;
    if (nIndex == s_nCreate)   return WelcomeCreate;
    for (unsigned e = 0; e < WelcomeRomCount; e++)
    {
        if (nIndex == s_nChoose[e]) return WelcomeAddRom;
        if (nIndex == s_nListen[e]) return e == WelcomeRom68k ? WelcomeListen68k : WelcomeListenPowerPC;
    }
    if (nIndex == s_nAddSys)   return WelcomeAddSystem;
    if (nIndex == s_nContinue) return WelcomeContinue;
    if (nIndex == s_nQuit)     return WelcomeQuit;
    return WelcomeNothing;
}

/*
 *  The SD cards inserted
 *
 *  A sheet over the page, as a new resolution is chosen: the list, a line that
 *  says what choosing does, Use and Cancel. Read again four times a second by
 *  the page's driver, so a card appears the moment it is inserted.
 */
static TPage     s_CardsPage;
static TWidget   s_CardsWidgets[12];
static TListItem s_CardsItems[WELCOME_CARDS];
static int s_nCardsList = -1, s_nCardsUse = -1, s_nCardsCancel = -1, s_nCardsNote = -1;

void WelcomeCardsDraw (TSurface *pSurface, TWelcomeCards *pCards)
{
    PageBeginAlert (&s_CardsPage, pSurface, s_CardsWidgets, sizeof s_CardsWidgets / sizeof s_CardsWidgets[0],
                    460, 240, Str (StrWelcomeCardsTitle));
    const TTheme &Theme = s_CardsPage.Theme;
    TLayout &Layout = s_CardsPage.Layout;
    PageFooter (&s_CardsPage);
    s_nCardsUse    = PageLast (&s_CardsPage, Str (StrWelcomeCardsUse), StateDefault);
    s_nCardsCancel = PageLast (&s_CardsPage, Str (StrCancel), StateNormal);

    s_nCardsNote = (int) s_CardsPage.nCount;
    PageAdd (&s_CardsPage, WidgetLabel, LayoutTop (&Layout, Theme.M.nLineHeight),
             Str (StrWelcomeCardsHint), StateDisabled);
    LayoutRowGap (&Layout);
    const unsigned nRoom = LayoutRoom (&Layout);
    const unsigned nReserve = 2 * ThemeReach (&Theme, StateFocused) + Theme.M.nGap / 2;
    s_nCardsList = (int) s_CardsPage.nCount;
    TWidget *pList = PageAdd (&s_CardsPage, WidgetList,
                              LayoutRow (&Layout, nRoom > nReserve ? nRoom - nReserve : 0, StateFocused),
                              nullptr, StateFocused);
    pList->pItems = s_CardsItems;
    pList->nChoice = pCards->nCount != 0 ? 0 : -1;
    WelcomeCardsSync (pCards);
    WelcomeCardsRepaint (pSurface);
}

void WelcomeCardsSync (TWelcomeCards *pCards)
{
    TWidget &List = s_CardsWidgets[s_nCardsList];
    for (unsigned i = 0; i < pCards->nCount && i < WELCOME_CARDS; i++)
    {
        s_CardsItems[i] = {};
        s_CardsItems[i].pText = pCards->Line[i];
    }
    if (pCards->nCount == 0)
    {
        // Said where the cards would be, so an empty list is not a fault.
        s_CardsItems[0] = {};
        s_CardsItems[0].pText = Str (StrWelcomeCardsNone);
        s_CardsItems[0].nState = StateDisabled;
        List.nItems = 1;
        List.nChoice = -1;
    }
    else
    {
        List.nItems = pCards->nCount;
        if (List.nChoice < 0 || List.nChoice >= (int) pCards->nCount)
            List.nChoice = 0;
    }
    WidgetSetState (&s_CardsWidgets[s_nCardsUse],
                    pCards->nCount != 0 ? StateDefault : StateDefault | StateDisabled);
}

void WelcomeCardsRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_CardsPage);
}

unsigned WelcomeCardsWidgets (TWidget **ppList)
{
    *ppList = s_CardsWidgets;
    return s_CardsPage.nCount;
}

TWelcomeCardsAction WelcomeCardsOperate (TWelcomeCards *pCards, int nIndex, int *pnChosen)
{
    if (nIndex == s_nCardsCancel)
        return WelcomeCardsCancel;
    const int nChoice = s_CardsWidgets[s_nCardsList].nChoice;
    if ((nIndex == s_nCardsUse || nIndex == s_nCardsList) && pCards->nCount != 0
        && nChoice >= 0 && nChoice < (int) pCards->nCount)
    {
        if (nIndex == s_nCardsList)
            return WelcomeCardsNothing;     // a row chosen; Use, or Return, uses it
        *pnChosen = nChoice;
        return WelcomeCardsUse;
    }
    return WelcomeCardsNothing;
}
