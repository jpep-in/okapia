/*
 * okapia_display.cpp — the display page.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_display.h"
#include "okapia_page.h"
#include "okapia_screen.h"
#include "okapia_strings.h"

static TPage s_Page;
static TWidget s_Widgets[24];
static TListItem s_Rows[VIDEO_SCREEN_SIZES_MAX + 1];
static TVideoScreenSize s_Sizes[VIDEO_SCREEN_SIZES_MAX];
static char s_Names[VIDEO_SCREEN_SIZES_MAX][56];
static char s_Output[20], s_Preferred[64];
static TListColumn s_Columns[2];
static TWidget *s_pList, *s_pRefresh, *s_pStatus;

static const struct { int nSkip; const char *pRate; } RefreshRates[] =
{
    { 0, nullptr }, { 1, "60" }, { 2, "30" }, { 4, "15" },
    { 6, "10" }, { 8, "7.5" }, { 12, "5" }
};
static const unsigned REFRESH_COUNT = sizeof RefreshRates / sizeof RefreshRates[0];
static TListItem s_RefreshItems[REFRESH_COUNT];
static char s_RefreshText[REFRESH_COUNT][16];

static TPage s_NewPage;
static TWidget s_NewWidgets[12];
static TWidget *s_pCreate, *s_pCancel, *s_pNewNote;
static char s_Width[6], s_Height[6];

static unsigned SizeText (char *pText, unsigned nCapacity, TVideoSize Size)
{
    unsigned n = StrAppendNumber (pText, nCapacity, 0, Size.nWidth);
    n = StrAppend (pText, nCapacity, n, " × ");
    return StrAppendNumber (pText, nCapacity, n, Size.nHeight);
}

static bool Earlier (const TVideoScreenSize &A, const TVideoScreenSize &B)
{
    return A.nWidth < B.nWidth || (A.nWidth == B.nWidth && A.nHeight < B.nHeight);
}

static void SelectSize (TVideoSize Size)
{
    for (unsigned i = 0; i + 1 < s_pList->nItems; i++)
    {
        if (VideoSizeEqual (Size, {s_Sizes[i].nWidth, s_Sizes[i].nHeight}))
        {
            s_pList->nChoice = (int) i + 1;
            WidgetListReveal (s_pList, &s_Page.Theme);
            return;
        }
    }
}

static void Rows (const TDisplaySettings *pDisplay)
{
    // The model needs only dimensions here. The two drivers supply their own IDs.
    static const uint32_t Ids[VIDEO_SCREEN_STANDARD] = {};
    static const uint32_t Extras[VIDEO_SCREEN_EXTRA] = {};
    const TVideoSize Output = pDisplay->Output;
    const TVideoSize Start = pDisplay->Start;
    const unsigned nSizes = VideoScreenSizeList (Ids, Extras, Output.nWidth, Output.nHeight,
                                          Start.nWidth, Start.nHeight, s_Sizes,
                                          &pDisplay->Modes);
    s_pList->nItems = nSizes + 1;
    s_Rows[0] = {};
    s_Rows[0].pText = Str (StrDisplayAdd);
    s_Rows[0].bAction = true;
    // Order only the presentation. The drivers keep their assigned IDs.
    for (unsigned i = 1; i < nSizes; i++)
    {
        const TVideoScreenSize Size = s_Sizes[i];
        unsigned j = i;
        while (j != 0 && Earlier (Size, s_Sizes[j - 1]))
        {
            s_Sizes[j] = s_Sizes[j - 1];
            j--;
        }
        s_Sizes[j] = Size;
    }

    const bool bStartFits = VideoScreenFits (Start, Output) && !VideoModeHidden (pDisplay->Modes, Start);
    const TVideoSize Selected = bStartFits ? Start : TVideoSize {640, 480};
    for (unsigned i = 0; i < nSizes; i++)
    {
        const TVideoSize Size = {s_Sizes[i].nWidth, s_Sizes[i].nHeight};
        const bool bFits = VideoScreenFits (Size, Output);
        const bool bHidden = VideoModeHidden (pDisplay->Modes, Size);
        const bool bFallback = VideoSizeEqual (Size, {640, 480});

        unsigned n = SizeText (s_Names[i], sizeof s_Names[i], Size);
        if (bFits)
        {
            const unsigned x = Output.nWidth / Size.nWidth;
            const unsigned y = Output.nHeight / Size.nHeight;
            const unsigned nScale = x < y ? x : y;
            n = StrAppend (s_Names[i], sizeof s_Names[i], n, "   ×");
            n = StrAppendNumber (s_Names[i], sizeof s_Names[i], n, nScale);
            if (Size.nWidth * nScale == Output.nWidth && Size.nHeight * nScale == Output.nHeight)
            {
                n = StrAppend (s_Names[i], sizeof s_Names[i], n, " · ");
                StrAppend (s_Names[i], sizeof s_Names[i], n, Str (StrDisplayFullScreen));
            }
        }
        else
        {
            n = StrAppend (s_Names[i], sizeof s_Names[i], n, "   ");
            StrAppend (s_Names[i], sizeof s_Names[i], n, Str (StrDisplayDoesNotFit));
        }
        TListItem &Row = s_Rows[i + 1];
        Row = {};
        Row.pText = s_Names[i];
        Row.nCell[0] = (bHidden ? StateNormal : StateChecked)
                            | (bFallback ? StateDisabled : StateNormal);
        Row.nCell[1] = (!bFits || bHidden ? StateDisabled : StateNormal)
                            | (bFits && !bHidden && VideoSizeEqual (Selected, Size)
                               ? StateChecked : StateNormal);
    }
}

static void InfoPair (TStringId Label, const char *pValue, const TRect &Band)
{
    TRow Row;
    RowBegin (&Row, &s_Page.Theme, Band);
    const char *pLabel = Str (Label);
    const unsigned nLabel = GfxTextWidth (s_Page.Theme.pBodyFont, pLabel);
    PageAdd (&s_Page, WidgetLabel, RowNext (&Row, nLabel, StateNormal), pLabel, StateDisabled);
    PageAdd (&s_Page, WidgetLabel, RowRest (&Row, StateNormal), pValue, StateNormal);
}

static void DrawMonitor (const TDisplaySettings *pDisplay)
{
    TLayout &Layout = s_Page.Layout;
    const TTheme &Theme = s_Page.Theme;
    TPageGroup Group;
    PageGroupBegin (&s_Page, &Group, Str (StrDisplayMonitorGroup));
    SizeText (s_Output, sizeof s_Output, pDisplay->Output);
    unsigned nPreferred = SizeText (s_Preferred, sizeof s_Preferred, pDisplay->Preferred);
    if (!VideoSizeEqual (pDisplay->Preferred, pDisplay->Output))
    {
        nPreferred = StrAppend (s_Preferred, sizeof s_Preferred, nPreferred, " — ");
        StrAppend (s_Preferred, sizeof s_Preferred, nPreferred,
                   Str (pDisplay->bWindowed ? StrDisplayMismatchWindow : StrDisplayMismatch));
    }

    const TStringId Left[] = {pDisplay->bWindowed ? StrDisplayOutputWindow : StrDisplayOutput,
                              StrDisplayMonitor};
    const TStringId Right[] = {pDisplay->bWindowed ? StrDisplayPreferredScreen : StrDisplayPreferred,
                               StrDisplayHdmi};
    const char *const ValuesLeft[] = {s_Output,
        *pDisplay->Monitor ? pDisplay->Monitor : Str (StrDisplayUnknown)};
    const char *const ValuesRight[] = {
        pDisplay->Preferred.nWidth != 0 ? s_Preferred : Str (StrDisplayUnknown),
        Str (!pDisplay->bConfigAvailable ? StrDisplayUnknown
             : pDisplay->bHdmiDirectives ? StrDisplayHdmiDirectives : StrDisplayHdmiAuto)
    };
    for (unsigned i = 0; i < 2; i++)
    {
        const TRect Band = LayoutTop (&Layout, Theme.M.nLineHeight);
        const unsigned nLeft = Band.nWidth * 2 / 5;
        const unsigned nRight = nLeft + Theme.M.nGap;
        InfoPair (Left[i], ValuesLeft[i], Rect (Band.nX, Band.nY, nLeft, Band.nHeight));
        // A window has no HDMI line to speak of: the second right-hand pair is
        // the board's alone.
        if (i == 0 || !pDisplay->bWindowed)
        {
            InfoPair (Right[i], ValuesRight[i], Rect (Band.nX + (int) nRight, Band.nY,
                                                     Band.nWidth - nRight, Band.nHeight));
        }
        if (i == 0)
            LayoutRowGap (&Layout);
    }
    PageGroupEnd (&s_Page, &Group);
}

static void DrawRefresh (const TDisplaySettings *pDisplay)
{
    int nChoice = 0;
    for (unsigned i = 0; i < REFRESH_COUNT; i++)
    {
        s_RefreshItems[i] = {};
        const char *pRate = RefreshRates[i].pRate;
        if (pRate == nullptr)
            s_RefreshItems[i].pText = Str (StrDynamic);
        else
        {
            unsigned n = StrAppend (s_RefreshText[i], sizeof s_RefreshText[i], 0, pRate);
            n = StrAppend (s_RefreshText[i], sizeof s_RefreshText[i], n, " ");
            StrAppend (s_RefreshText[i], sizeof s_RefreshText[i], n, Str (StrUnitHz));
            s_RefreshItems[i].pText = s_RefreshText[i];
        }
        if (RefreshRates[i].nSkip == pDisplay->nFrameSkip)
            nChoice = (int) i;
    }

    const TTheme &Theme = s_Page.Theme;
    const TRect Band = LayoutRow (&s_Page.Layout, Theme.M.nButtonHeight, StateFocused);
    TRow Row;
    RowBegin (&Row, &Theme, Band);
    const char *pLabel = Str (StrScreenRefresh);
    TRect Label = RowNext (&Row, GfxTextWidth (Theme.pBodyFont, pLabel), StateNormal);
    Label.nY += (int) (Band.nHeight - Theme.M.nLineHeight) / 2;
    Label.nHeight = Theme.M.nLineHeight;
    PageAdd (&s_Page, WidgetLabel, Label, pLabel, StateNormal);
    s_pRefresh = PageAdd (&s_Page, WidgetPopup, RowNext (&Row, 102 * s_Page.nScale16 / 16,
                           StateFocused), nullptr, StateNormal);
    s_pRefresh->pItems = s_RefreshItems;
    s_pRefresh->nItems = REFRESH_COUNT;
    s_pRefresh->nChoice = nChoice;
    TRect Status = RowRest (&Row, StateNormal);
    Status.nY += (int) (Band.nHeight - Theme.M.nLineHeight) / 2;
    Status.nHeight = Theme.M.nLineHeight;
    s_pStatus = PageAdd (&s_Page, WidgetLabel, Status, Str (StrDisplayModeHint), StateDisabled);
}

void DisplayDraw (TSurface *pSurface, TDisplaySettings *pDisplay)
{
    PageBeginTabbed (&s_Page, pSurface, s_Widgets, sizeof s_Widgets / sizeof s_Widgets[0],
                     PageTabDisplay);
    TLayout &Layout = s_Page.Layout;
    const TTheme &Theme = s_Page.Theme;
    DrawMonitor (pDisplay);
    LayoutSectionGap (&Layout);

    TPageGroup Group;
    PageGroupBegin (&s_Page, &Group, Str (StrDisplayMacGroup));
    DrawRefresh (pDisplay);
    LayoutRowGap (&Layout);
    const unsigned nReserve = 2 * ThemeReach (&Theme, StateFocused) + Theme.M.nGap / 2 + Theme.M.nStroke;
    const unsigned nRoom = LayoutRoom (&Layout);
    s_pList = PageAdd (&s_Page, WidgetList,
                      LayoutRow (&Layout, nRoom > nReserve ? nRoom - nReserve : 0, StateFocused),
                      nullptr, StateNormal);
    s_Columns[0] = {Str (StrDisplayListed), false};
    s_Columns[1] = {Str (StrDisplayStart), true};
    // Text rows have no folder icon. Leave room for a focused column mark.
    const unsigned nFocusedMark = Theme.M.nCheckSize + 2 * ThemeReach (&Theme, StateFocused);
    s_pList->nListRowHeight = nFocusedMark > Theme.M.nMenuRow ? nFocusedMark : Theme.M.nMenuRow;
    s_pList->pItems = s_Rows;
    s_pList->pColumns = s_Columns;
    s_pList->nColumns = 2;
    s_pList->nChoice = 0;
    Rows (pDisplay);
    const TVideoSize Start = pDisplay->Start;
    const TVideoSize Initial = VideoScreenFits (Start, pDisplay->Output)
                              && !VideoModeHidden (pDisplay->Modes, Start) ? Start : TVideoSize {640, 480};
    SelectSize (pDisplay->Selected.nWidth != 0 ? pDisplay->Selected : Initial);
    PageGroupEnd (&s_Page, &Group);
    DisplayRepaint (pSurface);
}

static unsigned Dimension (const char *pText)
{
    unsigned n = 0;
    for (; *pText != '\0'; pText++)
    {
        if (*pText < '0' || *pText > '9')
            return 0;
        n = 10 * n + (unsigned) (*pText - '0');
    }
    return n;
}

static TVideoSize EnteredSize (void)
{
    return {Dimension (s_Width), Dimension (s_Height)};
}

static TStringId SizeNote (const TDisplaySettings *pDisplay)
{
    const TVideoSize Size = EnteredSize ();
    if (*s_Width == '\0' || *s_Height == '\0')
        return StrDisplaySizeHint;
    if (!VideoCustomSizeValid (Size) || !VideoScreenFits (Size, pDisplay->Output))
        return StrDisplayBadSize;
    for (unsigned i = 0; i + 1 < s_pList->nItems; i++)
        if (VideoSizeEqual (Size, {s_Sizes[i].nWidth, s_Sizes[i].nHeight}))
            return StrDisplayDuplicate;
    for (const TVideoSize &Slot : pDisplay->Modes.Custom)
        if (Slot.nWidth == 0)
            return StrDisplayReady;
    return StrDisplayFull;
}

bool DisplayOperate (TDisplaySettings *pDisplay, int nIndex, unsigned nCell)
{
    if (nIndex < 0 || (unsigned) nIndex >= s_Page.nCount)
        return false;
    TWidget *pWidget = &s_Widgets[nIndex];
    if (pWidget == s_pRefresh)
    {
        const int nChoice = s_pRefresh->nChoice;
        if (nChoice >= 0 && (unsigned) nChoice < REFRESH_COUNT)
        {
            pDisplay->nFrameSkip = RefreshRates[nChoice].nSkip;
            pDisplay->bRefreshChanged = true;
        }
        return false;
    }
    if (pWidget != s_pList || s_pList->nChoice < 0)
        return false;
    if (s_pList->nChoice == 0)
        return nCell == 0;
    const unsigned i = (unsigned) s_pList->nChoice;
    if (i >= s_pList->nItems || nCell == 0 || nCell > 2
        || (s_Rows[i].nCell[nCell - 1] & StateDisabled) != 0)
        return false;

    const TVideoSize Size = {s_Sizes[i - 1].nWidth, s_Sizes[i - 1].nHeight};
    if (nCell == 1)
    {
        if (!VideoModeSetHidden (&pDisplay->Modes, Size, !VideoModeHidden (pDisplay->Modes, Size)))
        {
            s_pStatus->pText = Str (StrDisplayHiddenFull);
            return false;
        }
        pDisplay->bModesChanged = true;
    }
    else
    {
        pDisplay->Start = Size;
        pDisplay->bStartChanged = true;
    }
    s_pStatus->pText = Str (StrDisplayModeHint);
    pDisplay->Selected = Size;
    Rows (pDisplay);
    SelectSize (Size);
    return false;
}

void NewResolutionDraw (TSurface *pSurface, const TDisplaySettings *pDisplay)
{
    *s_Width = *s_Height = '\0';
    PageBeginAlert (&s_NewPage, pSurface, s_NewWidgets, sizeof s_NewWidgets / sizeof s_NewWidgets[0],
                    430, 202, Str (StrDisplayNewTitle));
    const TTheme &Theme = s_NewPage.Theme;
    TLayout &Layout = s_NewPage.Layout;
    PageFooter (&s_NewPage);
    const int nCreate = PageLast (&s_NewPage, Str (StrCreate), StateDefault);
    const int nCancel = PageLast (&s_NewPage, Str (StrCancel), StateNormal);
    s_pCreate = &s_NewWidgets[nCreate];
    s_pCancel = &s_NewWidgets[nCancel];

    char *const Fields[] = {s_Width, s_Height};
    const TStringId Labels[] = {StrDisplayWidth, StrDisplayHeight};
    const unsigned nLabel = 75 * s_NewPage.nScale16 / 16;
    for (unsigned i = 0; i < 2; i++)
    {
        const TRect Band = LayoutRow (&Layout, Theme.M.nFieldHeight, StateFocused);
        TRow Row;
        RowBegin (&Row, &Theme, Band);
        TRect Label = RowNext (&Row, nLabel, StateNormal);
        Label.nY += (int) (Band.nHeight - Theme.M.nLineHeight) / 2;
        Label.nHeight = Theme.M.nLineHeight;
        PageAdd (&s_NewPage, WidgetLabel, Label, Str (Labels[i]), StateNormal);
        TWidget *pField = PageAdd (&s_NewPage, WidgetField, RowRest (&Row, StateFocused),
                                  Fields[i], i == 0 ? StateFocused : StateNormal);
        pField->pEdit = Fields[i];
        pField->nEditSize = sizeof s_Width;
        LayoutRowGap (&Layout);
    }
    s_pNewNote = PageAdd (&s_NewPage, WidgetLabel, LayoutTop (&Layout, Theme.M.nLineHeight),
                          nullptr, StateDisabled);
    NewResolutionSync (pDisplay);
    NewResolutionRepaint (pSurface);
}

void NewResolutionSync (const TDisplaySettings *pDisplay, TScreen *pScreen)
{
    const TStringId Note = SizeNote (pDisplay);
    const char *pText = Str (Note);
    const bool bReady = Note == StrDisplayReady;
    if (pScreen != nullptr)
    {
        if (s_pNewNote->pText != pText)
            ScreenTouch (pScreen, (int) (s_pNewNote - s_NewWidgets));
        if (bReady == ((s_pCreate->nState & StateDisabled) != 0))
            ScreenTouch (pScreen, (int) (s_pCreate - s_NewWidgets));
    }
    s_pNewNote->pText = pText;
    WidgetSetState (s_pCreate, bReady ? StateDefault : StateDefault | StateDisabled);
}

TNewResolutionAction NewResolutionOperate (TDisplaySettings *pDisplay, int nIndex)
{
    if (nIndex < 0 || (unsigned) nIndex >= s_NewPage.nCount)
        return NewResolutionNothing;
    if (&s_NewWidgets[nIndex] == s_pCancel)
        return NewResolutionCancel;
    if (&s_NewWidgets[nIndex] != s_pCreate || SizeNote (pDisplay) != StrDisplayReady)
        return NewResolutionNothing;

    for (TVideoSize &Slot : pDisplay->Modes.Custom)
    {
        if (Slot.nWidth == 0)
        {
            Slot = EnteredSize ();
            VideoModeSetHidden (&pDisplay->Modes, Slot, false);
            pDisplay->bModesChanged = true;
            pDisplay->Selected = Slot;
            return NewResolutionCreate;
        }
    }
    return NewResolutionNothing;
}

void NewResolutionRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_NewPage);
}

unsigned NewResolutionWidgets (TWidget **ppList)
{
    *ppList = s_NewWidgets;
    return s_NewPage.nCount;
}

void DisplayRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

unsigned DisplayWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}
