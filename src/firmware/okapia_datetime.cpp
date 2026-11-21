/*
 * okapia_datetime.cpp — the date and time, a group of the settings page. See
 * okapia_datetime.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_datetime.h"
#include "okapia_clock.h"
#include "okapia_gfx.h"
#include "okapia_icons.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static TPage *s_pPage;

static int s_nNow     = -1;
static int s_nSource  = -1;
static int s_nZone    = -1;
static int s_nNetTime = -1;
static int s_nServer  = -1;
static int s_nHelp    = -1;

static char s_Now[96];

// The zones, and one more for a fixed offset the card carries.
static const unsigned ZONES_MAX = 32;
static char      s_ZoneText[ZONES_MAX][80];
static TListItem s_ZoneItems[ZONES_MAX];
static unsigned  s_nZoneItems;

static unsigned Length (const char *p)
{
    unsigned n = 0;
    while (p[n] != '\0')
    {
        n++;
    }
    return n;
}

static void ZoneItem (int nMinutes, const char *pName)
{
    char *pText = s_ZoneText[s_nZoneItems];
    pText[0] = '\0';
    ClockOffsetText (pText, sizeof s_ZoneText[0], nMinutes);
    unsigned n = StrAppend (pText, sizeof s_ZoneText[0], Length (pText), " · ");
    StrAppend (pText, sizeof s_ZoneText[0], n, pName);
    s_ZoneItems[s_nZoneItems].pText  = pText;
    s_ZoneItems[s_nZoneItems].pIcon  = 0;
    s_ZoneItems[s_nZoneItems].nState = StateNormal;
    s_nZoneItems++;
}

static void BuildZones (const TDateTime *pDateTime)
{
    // The standard offset first, then the cities: the number is what tells two
    // zones apart at a glance, as "FR · France" does in the network's list.
    s_nZoneItems = 0;
    const unsigned nCount = ClockZoneCount ();
    for (unsigned i = 0; i < nCount && s_nZoneItems < ZONES_MAX; i++)
    {
        ZoneItem (ClockZoneStandard (i), Str (ClockZoneName (i)));
    }
    // What an older card carries is offered as itself rather than silently
    // replaced by the nearest city — which would be another country's summer.
    if (pDateTime->Opened.nZone == nCount && s_nZoneItems < ZONES_MAX)
    {
        ZoneItem (pDateTime->V.nFixedMinutes, Str (StrZoneFixed));
    }
}

void DateTimeSync (TDateTime *pDateTime)
{
    if (s_pPage == 0)
    {
        return;
    }
    TWidget *w = s_pPage->pWidgets;
    s_Now[0] = '\0';
    ClockFormat (s_Now, sizeof s_Now, pDateTime->nLocal);

    // The source follows the date on its line, wherever the date ends.
    const TTheme *pTheme = &s_pPage->Theme;
    const TRect Line = Rect (w[s_nNow].Rect.nX, w[s_nNow].Rect.nY,
                             w[s_nNow].Rect.nWidth + w[s_nSource].Rect.nWidth,
                             w[s_nNow].Rect.nHeight);
    unsigned nDate = GfxTextWidth (pTheme->pBodyBoldFont, s_Now) + pTheme->M.nGap;
    nDate = nDate < Line.nWidth ? nDate : Line.nWidth;
    w[s_nNow].Rect.nWidth = nDate;
    w[s_nSource].Rect = Rect (Line.nX + (int) nDate, Line.nY, Line.nWidth - nDate, Line.nHeight);

    if (s_nZone < 0)
    {
        return;                         // the host's clock: nothing more to show
    }
    w[s_nZone].nChoice = pDateTime->V.nZone < s_nZoneItems ? (int) pDateTime->V.nZone : 0;
    WidgetSetState (&w[s_nNetTime], pDateTime->V.bNetTime ? StateChecked : StateNormal);
    // A server nobody will ask needs no name: grey, as the shared folder's
    // path is when there is no folder.
    WidgetSetState (&w[s_nServer], pDateTime->V.bNetTime ? StateNormal : StateDisabled);
}

void DateTimeLayOut (TPage *pPage, TDateTime *pDateTime, unsigned nLabel)
{
    s_pPage = pPage;
    BuildZones (pDateTime);
    const TTheme *pTheme = &pPage->Theme;
    TLayout *pLayout = &pPage->Layout;

    TPageGroup Group;
    PageGroupBegin (pPage, &Group, Str (StrClockGroup));

    // The answer first: what time it is, in bold, and after it, why. At the
    // end of the line, (i): how the clock is kept and set, which the group has
    // no room to say.
    {
        const unsigned nIcon = pTheme->M.nLineHeight;
        TRow R;
        RowBegin (&R, pTheme, LayoutRow (pLayout, nIcon, StateFocused));
        s_nHelp = (int) pPage->nCount;
        PageAdd (pPage, WidgetIconButton, RowLast (&R, nIcon, StateFocused), 0, StateNormal)
            ->Paint = OkapiaPaintInfo;
        TRect Line = RowRest (&R, StateNormal);
        Line = Rect (Line.nX, Line.nY + (int) (nIcon - pTheme->M.nLineHeight) / 2,
                     Line.nWidth, pTheme->M.nLineHeight);
        s_nNow = (int) pPage->nCount;
        PageAdd (pPage, WidgetLabel, Line, s_Now, StateStrong);
        s_nSource = (int) pPage->nCount;
        PageAdd (pPage, WidgetLabel, Rect (Line.nX + (int) Line.nWidth, Line.nY, 0, Line.nHeight),
                 pDateTime->Source, StateDisabled);
    }
    s_nZone = s_nNetTime = s_nServer = -1;
    if (pDateTime->bHostClock)
    {
        PageGroupEnd (pPage, &Group);
        DateTimeSync (pDateTime);
        return;
    }
    LayoutRowGap (pLayout);

    // Where this is.
    const unsigned nPopup = pTheme->M.nButtonHeight;
    {
        TRow R;
        RowBegin (&R, pTheme, LayoutRow (pLayout, nPopup, StateFocused));
        TRect L = RowNext (&R, nLabel, StateNormal);
        L = Rect (L.nX, L.nY + (int) (nPopup - pTheme->M.nLineHeight) / 2, L.nWidth,
                  pTheme->M.nLineHeight);
        PageAdd (pPage, WidgetLabel, L, Str (StrTimeZone), StateNormal);
        s_nZone = (int) pPage->nCount;
        TWidget *p = PageAdd (pPage, WidgetPopup, RowRest (&R, StateFocused), 0, StateNormal);
        p->pItems = s_ZoneItems;
        p->nItems = s_nZoneItems;
    }
    LayoutRowGap (pLayout);

    // Whom to ask: whether and who on one line, as the shared folder does.
    {
        TRow R;
        RowBegin (&R, pTheme, LayoutRow (pLayout, pTheme->M.nFieldHeight, StateFocused));
        const unsigned nTick = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, Str (StrNetTime));
        TRect Box = RowNext (&R, nTick > nLabel ? nTick : nLabel, StateNormal);
        Box = Rect (Box.nX, Box.nY + (int) (pTheme->M.nFieldHeight - pTheme->M.nCheckSize) / 2,
                    nTick, pTheme->M.nCheckSize);
        s_nNetTime = (int) pPage->nCount;
        PageAdd (pPage, WidgetCheckbox, Box, Str (StrNetTime), StateNormal);
        s_nServer = (int) pPage->nCount;
        TWidget *p = PageAdd (pPage, WidgetField, RowRest (&R, StateFocused),
                              pDateTime->V.Server, StateNormal);
        p->pEdit     = pDateTime->V.Server;
        p->nEditSize = DATETIME_SERVER;
    }

    PageGroupEnd (pPage, &Group);
    DateTimeSync (pDateTime);
}

TDateTimeAction DateTimeOperate (TDateTime *pDateTime, int nIndex)
{
    if (s_pPage == 0)
    {
        return DateTimeNothing;
    }
    TDateTimeAction Action = DateTimeNothing;
    if (nIndex == s_nZone)
    {
        const int n = s_pPage->pWidgets[s_nZone].nChoice;
        if (n >= 0 && (unsigned) n < s_nZoneItems && (unsigned) n != pDateTime->V.nZone)
        {
            pDateTime->V.nZone = (unsigned) n;
            Action = DateTimeZoneChanged;
        }
    }
    else if (nIndex == s_nHelp)
    {
        return DateTimeHelp;
    }
    else if (nIndex == s_nNetTime)
    {
        pDateTime->V.bNetTime = !pDateTime->V.bNetTime;
        Action = DateTimeServerChanged;
    }
    if (Action != DateTimeNothing)
    {
        DateTimeSync (pDateTime);
    }
    return Action;
}
