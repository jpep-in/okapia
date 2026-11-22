/*
 * okapia_network.cpp — the network page. See okapia_network.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_network.h"
#include "okapia_icons.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 28;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

static int s_nKind[NetworkKindCount] = { -1, -1, -1 };
static int s_nList     = -1;
static int s_nName     = -1;
static int s_nPassword = -1;
static int s_nShow     = -1;
static int s_nCountry  = -1;
static int s_nSearch   = -1;
static int s_nJoin     = -1;
static int s_nStatus   = -1;
// The labels in front of the Wi-Fi's controls, greyed with them.
static int s_nLabels[3] = { -1, -1, -1 };

static TListItem s_Rows[NETWORK_SEEN];
static char      s_Status[128];

/*
 *  Countries
 *
 *  The supplicant will not join without one: it decides which channels may be
 *  used. Each in its own language, like the languages menu, and a code the
 *  card carries that is not in the table is offered as itself rather than
 *  silently replaced.
 */
static const struct { const char *pCode; const char *pName; } COUNTRIES[] =
{
    { "AT", "Österreich" },     { "AU", "Australia" },      { "BE", "Belgique" },
    { "CA", "Canada" },         { "CH", "Schweiz" },        { "DE", "Deutschland" },
    { "DK", "Danmark" },        { "ES", "España" },         { "FI", "Suomi" },
    { "FR", "France" },         { "GB", "United Kingdom" }, { "IE", "Ireland" },
    { "IT", "Italia" },         { "JP", "Japan" },          { "LU", "Luxembourg" },
    { "NL", "Nederland" },      { "NO", "Norge" },          { "NZ", "New Zealand" },
    { "PL", "Polska" },         { "PT", "Portugal" },       { "SE", "Sverige" },
    { "US", "United States" }
};
static const unsigned COUNTRY_COUNT = sizeof COUNTRIES / sizeof COUNTRIES[0];
static char      s_CountryText[COUNTRY_COUNT + 1][32];
static TListItem s_CountryItems[COUNTRY_COUNT + 1];
static unsigned  s_nCountryItems;

static bool Same (const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
    {
        a++;
        b++;
    }
    return *a == *b;
}

static void Copy (char *pOut, unsigned nSize, const char *pIn)
{
    pOut[0] = '\0';
    StrAppend (pOut, nSize, 0, pIn);
}

static void BuildCountries (const TNetwork *pNetwork)
{
    s_nCountryItems = 0;
    bool bKnown = false;
    for (unsigned i = 0; i < COUNTRY_COUNT; i++)
    {
        char *pText = s_CountryText[s_nCountryItems];
        unsigned n = StrAppend (pText, sizeof s_CountryText[0], 0, COUNTRIES[i].pCode);
        n = StrAppend (pText, sizeof s_CountryText[0], n, " · ");
        StrAppend (pText, sizeof s_CountryText[0], n, COUNTRIES[i].pName);
        s_CountryItems[s_nCountryItems].pText  = pText;
        s_CountryItems[s_nCountryItems].pIcon  = 0;
        s_CountryItems[s_nCountryItems].nState = StateNormal;
        s_nCountryItems++;
        bKnown = bKnown || Same (COUNTRIES[i].pCode, pNetwork->V.Country);
    }
    if (!bKnown && pNetwork->V.Country[0] != '\0')
    {
        Copy (s_CountryText[s_nCountryItems], sizeof s_CountryText[0], pNetwork->V.Country);
        s_CountryItems[s_nCountryItems].pText  = s_CountryText[s_nCountryItems];
        s_CountryItems[s_nCountryItems].pIcon  = 0;
        s_CountryItems[s_nCountryItems].nState = StateNormal;
        s_nCountryItems++;
    }
}

static int CountryIndex (const char *pCode)
{
    for (unsigned i = 0; i < COUNTRY_COUNT; i++)
    {
        if (Same (COUNTRIES[i].pCode, pCode))
        {
            return (int) i;
        }
    }
    // The extra entry, when the card carries a code of its own.
    return s_nCountryItems > COUNTRY_COUNT ? (int) COUNTRY_COUNT : -1;
}

/*
 *  The model's rules
 */

unsigned NetworkBars (int nDbm)
{
    return nDbm >= -55 ? 4 : nDbm >= -65 ? 3 : nDbm >= -75 ? 2 : 1;
}

static TIconPainter SignalMark (int nDbm)
{
    static const TIconPainter Bars[4] =
    {
        OkapiaPaintSignal1, OkapiaPaintSignal2, OkapiaPaintSignal3, OkapiaPaintSignal4
    };
    return Bars[NetworkBars (nDbm) - 1];
}

unsigned NetworkSortSeen (TNetworkSeen *pSeen, unsigned nCount)
{
    // Hidden networks out first, then the weaker twin of every name: an access
    // point and its repeater are one network to whoever is choosing.
    unsigned n = 0;
    for (unsigned i = 0; i < nCount; i++)
    {
        if (pSeen[i].SSID[0] == '\0')
        {
            continue;
        }
        bool bTwin = false;
        for (unsigned j = 0; j < n; j++)
        {
            if (Same (pSeen[j].SSID, pSeen[i].SSID))
            {
                if (pSeen[i].nDbm > pSeen[j].nDbm)
                {
                    pSeen[j] = pSeen[i];
                }
                bTwin = true;
                break;
            }
        }
        if (!bTwin)
        {
            pSeen[n++] = pSeen[i];
        }
    }
    // Strongest first, by insertion: at two dozen entries nothing else is
    // worth its code, and it keeps equal signals in the order they came.
    for (unsigned i = 1; i < n; i++)
    {
        const TNetworkSeen Item = pSeen[i];
        unsigned j = i;
        while (j > 0 && pSeen[j - 1].nDbm < Item.nDbm)
        {
            pSeen[j] = pSeen[j - 1];
            j--;
        }
        pSeen[j] = Item;
    }
    return n < NETWORK_SEEN ? n : NETWORK_SEEN;
}

bool NetworkPasswordUsable (const char *pPassword)
{
    unsigned n = 0;
    bool bHex = true, bPrintable = true;
    for (const char *p = pPassword; *p != '\0'; p++, n++)
    {
        const char c = *p;
        bHex = bHex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                        || (c >= 'A' && c <= 'F'));
        bPrintable = bPrintable && c >= 32 && c <= 126;
    }
    return n == 0 || (n == 64 && bHex) || (n >= 8 && n <= 63 && bPrintable);
}

bool NetworkNeedsRestart (const TNetwork *pNetwork)
{
    return pNetwork->nRunning != NetworkOff && pNetwork->V.nKind != pNetwork->nRunning;
}

static bool Available (const TNetwork *pNetwork, unsigned nKind)
{
    return pNetwork->nAvailable == 0 || (pNetwork->nAvailable & (1u << nKind)) != 0;
}

/*
 *  The status line
 */

static void Say (TStringId Id)
{
    Copy (s_Status, sizeof s_Status, Str (Id));
}

// "Connected to “Maison”", and the rest after it.
static unsigned SayAbout (TStringId Id, const char *pSSID)
{
    unsigned n = StrAppend (s_Status, sizeof s_Status, 0, Str (Id));
    n = StrAppend (s_Status, sizeof s_Status, n, " “");
    n = StrAppend (s_Status, sizeof s_Status, n, pSSID);
    return StrAppend (s_Status, sizeof s_Status, n, "”");
}

static void StatusText (const TNetwork *pNetwork)
{
    s_Status[0] = '\0';
    const unsigned nKind = pNetwork->V.nKind;

    if (NetworkNeedsRestart (pNetwork))
    {
        Say (StrNetStRestart);
        return;
    }
    if (nKind == NetworkOff)
    {
        Say (StrNetStOff);
        return;
    }
    if (!Available (pNetwork, nKind) || pNetwork->State == NetStateUnavailable)
    {
        Say (nKind == NetworkWiFi ? StrNetStNoWifi : StrNetStNoCable);
        return;
    }
    if (nKind == NetworkCable)
    {
        if (pNetwork->State == NetStateCableUp)
        {
            unsigned n = StrAppend (s_Status, sizeof s_Status, 0,
                                    Str (StrNetStCableUp));
            if (pNetwork->nMbps != 0)
            {
                n = StrAppend (s_Status, sizeof s_Status, n, " · ");
                n = StrAppendNumber (s_Status, sizeof s_Status, n, pNetwork->nMbps);
                StrAppend (s_Status, sizeof s_Status, n, " Mbit/s");
            }
        }
        else if (pNetwork->State == NetStateCableDown)
        {
            Say (StrNetStCableDown);
        }
        return;
    }

    // The Wi-Fi. What the page itself refused comes first: it is the answer to
    // the button just pressed.
    if (pNetwork->bNoName)
    {
        Say (StrNetStNoName);
        return;
    }
    if (pNetwork->bBadPassword)
    {
        Say (StrNetStBadPassword);
        return;
    }
    switch (pNetwork->State)
    {
    case NetStateStarting:  Say (StrNetStStarting);   break;
    case NetStateSearching: Say (StrNetStSearching);  break;
    case NetStateReady:     Say (StrNetStChoose);     break;
    case NetStateJoining:
        {
            const unsigned n = SayAbout (StrNetStJoining, pNetwork->Subject);
            StrAppend (s_Status, sizeof s_Status, n, "…");
        }
        break;
    case NetStateConnected:
        {
            unsigned n = SayAbout (StrNetStConnected, pNetwork->Subject);
            if (pNetwork->nDbm < 0)
            {
                n = StrAppend (s_Status, sizeof s_Status, n, " · -");
                n = StrAppendNumber (s_Status, sizeof s_Status, n,
                                     (unsigned long) -pNetwork->nDbm);
                StrAppend (s_Status, sizeof s_Status, n, " dBm");
            }
        }
        break;
    case NetStateRefused:   SayAbout (StrNetStRefused, pNetwork->Subject); break;
    case NetStateLost:      SayAbout (StrNetStLost, pNetwork->Subject);    break;
    default:                break;
    }
}

/*
 *  Components
 */

unsigned NetworkWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

TRect NetworkDialog (void)
{
    return s_Page.Dialog;
}

bool NetworkOverflowed (void)
{
    return s_Page.Layout.bOverflow;
}

void NetworkSync (TNetwork *pNetwork)
{
    const bool bWiFi = pNetwork->V.nKind == NetworkWiFi;
    // The Wi-Fi's controls answer only while the Wi-Fi is what is chosen and
    // what this board can run without a restart. Greyed, not hidden: a page
    // whose shape changes with a radio button reads as two pages.
    const bool bLive = bWiFi && Available (pNetwork, NetworkWiFi)
                    && !NetworkNeedsRestart (pNetwork)
                    && pNetwork->State != NetStateUnavailable;
    const unsigned nWiFi = bWiFi && Available (pNetwork, NetworkWiFi)
                         ? StateNormal : StateDisabled;

    for (unsigned i = 0; i < NetworkKindCount; i++)
    {
        WidgetSetState (&s_Widgets[s_nKind[i]],
                        (pNetwork->V.nKind == i ? StateChecked : StateNormal)
                        | (Available (pNetwork, i) ? StateNormal : StateDisabled));
    }

    // The rows: name, lock, bars — and the bullet a list gives the row it has
    // singled out, for the network actually joined.
    TWidget *pList = &s_Widgets[s_nList];
    pList->nItems  = pNetwork->nSeen;
    pList->nChoice = -1;
    for (unsigned i = 0; i < pNetwork->nSeen && i < NETWORK_SEEN; i++)
    {
        const TNetworkSeen *p = &pNetwork->Seen[i];
        TListItem *pRow = &s_Rows[i];
        pRow->pText    = p->SSID;
        pRow->pIcon    = 0;
        pRow->bAction  = false;
        pRow->Marks[0] = p->bSecured ? OkapiaPaintLock : 0;
        pRow->Marks[1] = SignalMark (p->nDbm);
        pRow->nState   = (pNetwork->State == NetStateConnected
                          && Same (p->SSID, pNetwork->Subject)) ? StateChecked : StateNormal;
        if (!bLive)
        {
            pRow->nState |= StateDisabled;
        }
        if (Same (p->SSID, pNetwork->V.SSID))
        {
            pList->nChoice = (int) i;
        }
    }
    if (pList->nTop + WidgetListVisible (pList, &s_Page.Theme) > pList->nItems)
    {
        pList->nTop = 0;
    }
    WidgetListReveal (pList, &s_Page.Theme);
    WidgetSetState (pList, bLive ? StateNormal : StateDisabled);

    WidgetSetState (&s_Widgets[s_nName], nWiFi);
    WidgetSetState (&s_Widgets[s_nPassword], nWiFi);
    s_Widgets[s_nPassword].bSecret = !pNetwork->bShowPassword;
    WidgetSetState (&s_Widgets[s_nShow],
                    nWiFi | (pNetwork->bShowPassword ? StateChecked : StateNormal));
    s_Widgets[s_nCountry].nChoice = CountryIndex (pNetwork->V.Country);
    WidgetSetState (&s_Widgets[s_nCountry], nWiFi);
    for (unsigned i = 0; i < 3; i++)
    {
        WidgetSetState (&s_Widgets[s_nLabels[i]], nWiFi);
    }
    WidgetSetState (&s_Widgets[s_nSearch], bLive ? StateNormal : StateDisabled);
    WidgetSetState (&s_Widgets[s_nJoin], nWiFi);

    StatusText (pNetwork);
}

// A label on the left at the control's height, and what is left of the row.
static TRect Labelled (TRow *pRow, unsigned nLabel, unsigned nHeight, TStringId Id, int *pnIndex)
{
    const TTheme *pTheme = &s_Page.Theme;
    TRect L = RowNext (pRow, nLabel, StateNormal);
    L = Rect (L.nX, L.nY + (int) (nHeight - pTheme->M.nLineHeight) / 2, L.nWidth,
              pTheme->M.nLineHeight);
    *pnIndex = (int) s_Page.nCount;
    PageAdd (&s_Page, WidgetLabel, L, Str (Id), StateNormal);
    return RowRest (pRow, StateFocused);
}

void NetworkDraw (TSurface *pSurface, TNetwork *pNetwork)
{
    BuildCountries (pNetwork);
    PageBeginTabbed (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, PageTabNetwork);
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    // One label column, measured in this language over every label it holds.
    static const TStringId Labels[] =
    {
        StrNetConnection, StrNetName, StrNetPassword, StrNetCountry
    };
    unsigned nLabel = 0;
    for (unsigned i = 0; i < sizeof Labels / sizeof Labels[0]; i++)
    {
        const unsigned n = GfxTextWidth (pTheme->pBodyFont, Str (Labels[i]));
        nLabel = n > nLabel ? n : nLabel;
    }
    nLabel += pTheme->M.nGap;

    // Which connection, across the top: the question everything under it
    // depends on.
    {
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nCheckSize, StateFocused));
        TRect L = RowNext (&Band, nLabel, StateNormal);
        L = Rect (L.nX, L.nY + ((int) pTheme->M.nCheckSize - (int) pTheme->M.nLineHeight) / 2,
                  L.nWidth, pTheme->M.nLineHeight);
        PageAdd (&s_Page, WidgetLabel, L, Str (StrNetConnection), StateNormal);
        const TStringId Kinds[NetworkKindCount] =
            { StrNetOff, StrNetCable, StrNetWifi };
        for (unsigned i = 0; i < NetworkKindCount; i++)
        {
            const unsigned nWide = pTheme->M.nCheckSize + pTheme->M.nGap
                                 + GfxTextWidth (pTheme->pBodyFont, Str (Kinds[i]));
            s_nKind[i] = (int) s_Page.nCount;
            TWidget *p = PageAdd (&s_Page, WidgetRadio, RowNext (&Band, nWide, StateFocused),
                                  Str (Kinds[i]), StateNormal);
            p->nGroup = 1;
            RowSkip (&Band, pTheme->M.nGap);
        }
    }
    LayoutSectionGap (&Layout);

    // From the bottom up, so the list takes whatever the rest leaves: the
    // status line, then the country with the two actions, the password, the
    // name.
    s_nStatus = (int) s_Page.nCount;
    PageAdd (&s_Page, WidgetLabel, LayoutRowBottom (&Layout, pTheme->M.nLineHeight, StateNormal),
             s_Status, StateNormal);
    LayoutRowGapBottom (&Layout);

    {
        TRow Band;
        RowBegin (&Band, pTheme,
                  LayoutRowBottom (&Layout, pTheme->M.nButtonHeight, StateFocused));
        // The assent of this part of the page on the right, as everywhere.
        const unsigned nJoin   = WidgetButtonWidth (pTheme, Str (StrNetJoin), StateNormal);
        const unsigned nSearch = WidgetButtonWidth (pTheme, Str (StrNetSearch), StateNormal);
        s_nJoin = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetButton, RowLast (&Band, nJoin, StateFocused),
                 Str (StrNetJoin), StateNormal);
        s_nSearch = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetButton, RowLast (&Band, nSearch, StateFocused),
                 Str (StrNetSearch), StateNormal);
        const TRect R = Labelled (&Band, nLabel, pTheme->M.nButtonHeight, StrNetCountry,
                                  &s_nLabels[2]);
        s_nCountry = (int) s_Page.nCount;
        TWidget *p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
        p->pItems = s_CountryItems;
        p->nItems = s_nCountryItems;
    }
    LayoutRowGapBottom (&Layout);

    {
        TRow Band;
        RowBegin (&Band, pTheme,
                  LayoutRowBottom (&Layout, pTheme->M.nFieldHeight, StateFocused));
        const unsigned nShow = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, Str (StrNetShow));
        TRect Show = RowLast (&Band, nShow, StateFocused);
        Show = Rect (Show.nX,
                     Show.nY + (int) (pTheme->M.nFieldHeight - pTheme->M.nCheckSize) / 2,
                     Show.nWidth, pTheme->M.nCheckSize);
        s_nShow = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetCheckbox, Show, Str (StrNetShow), StateNormal);
        const TRect R = Labelled (&Band, nLabel, pTheme->M.nFieldHeight, StrNetPassword,
                                  &s_nLabels[1]);
        s_nPassword = (int) s_Page.nCount;
        TWidget *p = PageAdd (&s_Page, WidgetField, R, pNetwork->V.Password, StateNormal);
        p->pEdit     = pNetwork->V.Password;
        p->nEditSize = NETWORK_PASSWORD;
        p->bSecret   = true;
    }
    LayoutRowGapBottom (&Layout);

    {
        TRow Band;
        RowBegin (&Band, pTheme,
                  LayoutRowBottom (&Layout, pTheme->M.nFieldHeight, StateFocused));
        const TRect R = Labelled (&Band, nLabel, pTheme->M.nFieldHeight, StrNetName,
                                  &s_nLabels[0]);
        s_nName = (int) s_Page.nCount;
        TWidget *p = PageAdd (&s_Page, WidgetField, R, pNetwork->V.SSID, StateNormal);
        p->pEdit     = pNetwork->V.SSID;
        p->nEditSize = NETWORK_SSID;
    }
    LayoutSectionGapBottom (&Layout);

    // The networks in range take the rest.
    s_nList = (int) s_Page.nCount;
    TWidget *pList = PageAdd (&s_Page, WidgetList,
                              LayoutRow (&Layout, LayoutRoom (&Layout)
                                                  - 2 * ThemeReach (pTheme, StateFocused),
                                         StateFocused),
                              0, StateNormal);
    pList->pItems = s_Rows;

    NetworkSync (pNetwork);
    NetworkRepaint (pSurface);
}

void NetworkRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

TNetworkAction NetworkOperate (TNetwork *pNetwork, int nIndex)
{
    TNetworkAction Action = NetworkNothing;
    const bool bWasRestart = NetworkNeedsRestart (pNetwork);

    // Anything touched clears what the page said about the last Join: the
    // sentence answered a button, and the user has moved on from it.
    pNetwork->bBadPassword = false;
    pNetwork->bNoName      = false;

    for (unsigned i = 0; i < NetworkKindCount; i++)
    {
        if (nIndex == s_nKind[i] && Available (pNetwork, i) && pNetwork->V.nKind != i)
        {
            pNetwork->V.nKind = i;
            Action = NetworkKindChanged;
        }
    }

    if (nIndex == s_nList)
    {
        const int n = s_Widgets[s_nList].nChoice;
        if (n >= 0 && n < (int) pNetwork->nSeen)
        {
            // Another network is another password: what was typed for the last
            // one is not offered to this one.
            if (!Same (pNetwork->V.SSID, pNetwork->Seen[n].SSID))
            {
                Copy (pNetwork->V.SSID, NETWORK_SSID, pNetwork->Seen[n].SSID);
                pNetwork->V.Password[0] = '\0';
            }
            // The fields hold their own caret, which pointed into the text
            // they had before.
            s_Widgets[s_nName].nCaret = s_Widgets[s_nName].nAnchor = 0;
            s_Widgets[s_nPassword].nCaret = s_Widgets[s_nPassword].nAnchor = 0;
        }
    }
    else if (nIndex == s_nShow)
    {
        pNetwork->bShowPassword = !pNetwork->bShowPassword;
    }
    else if (nIndex == s_nCountry)
    {
        const int n = s_Widgets[s_nCountry].nChoice;
        if (n >= 0 && n < (int) COUNTRY_COUNT)
        {
            Copy (pNetwork->V.Country, NETWORK_COUNTRY, COUNTRIES[n].pCode);
        }
    }
    else if (nIndex == s_nSearch)
    {
        Action = NetworkSearch;
    }
    else if (nIndex == s_nJoin)
    {
        if (pNetwork->V.SSID[0] == '\0')
        {
            pNetwork->bNoName = true;
        }
        else if (!NetworkPasswordUsable (pNetwork->V.Password))
        {
            pNetwork->bBadPassword = true;
        }
        else
        {
            Action = NetworkJoin;
        }
    }

    NetworkSync (pNetwork);
    if (NetworkNeedsRestart (pNetwork) != bWasRestart)
    {
        // Start changes its words; the caller still has to hear about the
        // kind, and does so through the restart it now owes.
        return NetworkRelayout;
    }
    return Action;
}
