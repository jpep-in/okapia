/*
 * okapia_chooser.cpp — the screen that asks which System to start.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_chooser.h"
#include "okapia_layout.h"
#include "okapia_strings.h"

// The screen's own array. Laid out once and then only restated, so that the
// list keeps pointing at the same rows while the user works.
static const unsigned MAX_WIDGETS = 24;
static TWidget   s_Widgets[MAX_WIDGETS];
static unsigned  s_nWidgets;
static TRect     s_Dialog;
static unsigned  s_nScale16 = 16;

// What each row says, and what the selected volume's line says underneath. Both
// are rebuilt in place, so nothing has to be laid out again when a tick box
// changes what a volume is.
static TListItem s_Items[CHOOSER_MAX];
static char      s_Rows[CHOOSER_MAX][72];
static char      s_Detail[96];

// The components the rest of this file needs to reach again.
static int s_nList     = -1;
static int s_nStartup  = -1;            // the "startup disk" tick box
static int s_nReadOnly = -1;
static int s_nMounted  = -1;
static int s_nDetail   = -1;
static int s_nStart    = -1;
static int s_nSettings = -1;
static int s_nPram     = -1;
static int s_nPower    = -1;

static inline int S (int nValue)
{
    return nValue * (int) s_nScale16 / 16;
}

/*
 *  Text, put together without a printf
 *
 *  The firmware has no format string anywhere else and there is no reason to
 *  start here: three appends say what one format would, and they cannot run off
 *  the end of the buffer while doing it.
 */
static unsigned Append (char *pOut, unsigned nSize, unsigned nAt, const char *pWhat)
{
    if (pWhat == 0)
    {
        return nAt;
    }
    while (*pWhat != '\0' && nAt + 1 < nSize)
    {
        pOut[nAt++] = *pWhat++;
    }
    pOut[nAt] = '\0';
    return nAt;
}

static unsigned AppendNumber (char *pOut, unsigned nSize, unsigned nAt, unsigned long nValue)
{
    char Digits[12];
    unsigned n = 0;
    do
    {
        Digits[n++] = (char) ('0' + (nValue % 10));
        nValue /= 10;
    }
    while (nValue != 0 && n < sizeof Digits);

    while (n-- > 0 && nAt + 1 < nSize)
    {
        pOut[nAt++] = Digits[n];
    }
    pOut[nAt] = '\0';
    return nAt;
}

// "Macintosh HD — System 7.1.2" — and what is wrong with it, when something is.
// The state belongs on the row rather than only under the selection: a card
// where one volume is in use has to say so without being clicked on.
static void RowText (const TChooser *pChooser, unsigned i)
{
    const TChooserVolume *p = &pChooser->Volumes[i];
    char *pOut = s_Rows[i];
    unsigned n = Append (pOut, sizeof s_Rows[i], 0, p->Name);

    if (p->System[0] != '\0')
    {
        n = Append (pOut, sizeof s_Rows[i], n, " — ");
        n = Append (pOut, sizeof s_Rows[i], n, "System ");
        n = Append (pOut, sizeof s_Rows[i], n, p->System);
    }
    else
    {
        n = Append (pOut, sizeof s_Rows[i], n, " — ");
        n = Append (pOut, sizeof s_Rows[i], n, Str (StrNoSystem));
    }
    if (!p->bClean)
    {
        n = Append (pOut, sizeof s_Rows[i], n, " · ");
        n = Append (pOut, sizeof s_Rows[i], n, Str (StrVolumeInUse));
    }
    if (p->bReadOnly)
    {
        n = Append (pOut, sizeof s_Rows[i], n, " · ");
        n = Append (pOut, sizeof s_Rows[i], n, Str (StrReadOnly));
    }
    if (!p->bMounted)
    {
        n = Append (pOut, sizeof s_Rows[i], n, " · ");
        Append (pOut, sizeof s_Rows[i], n, Str (StrNotMounted));
    }
}

// The line under the list: what the selected volume is, in numbers.
static void DetailText (const TChooser *pChooser, int nSel)
{
    s_Detail[0] = '\0';
    if (nSel < 0 || nSel >= (int) pChooser->nCount)
    {
        return;
    }
    const TChooserVolume *p = &pChooser->Volumes[nSel];
    unsigned n = Append (s_Detail, sizeof s_Detail, 0, p->Path);
    n = Append (s_Detail, sizeof s_Detail, n, " · ");
    n = AppendNumber (s_Detail, sizeof s_Detail, n, p->nFreeKB / 1024);
    n = Append (s_Detail, sizeof s_Detail, n, " ");
    n = Append (s_Detail, sizeof s_Detail, n, Str (StrMbFree));
    n = Append (s_Detail, sizeof s_Detail, n, " · ");
    Append (s_Detail, sizeof s_Detail, n, p->bClean ? Str (StrVolumeClean)
                                                    : Str (StrVolumeInUse));
}

static TWidget *Add (TWidgetType Type, const TRect &rRect, const char *pText, unsigned nState)
{
    if (s_nWidgets >= MAX_WIDGETS)
    {
        return &s_Widgets[MAX_WIDGETS - 1];
    }
    TWidget *p = &s_Widgets[s_nWidgets++];
    WidgetClear (p);
    p->Type   = Type;
    p->Rect   = rRect;
    p->pText  = pText;
    p->nState = nState;
    return p;
}

// Placed with the room a focus ring needs whatever state it is drawn in: the
// layout runs before the loop and cannot know which control will be focused.
static void AddButton (TRow *pRow, const char *pText, unsigned nState, int *pIndex)
{
    const unsigned nWidth = WidgetButtonWidth (pRow->pTheme, pText, nState);
    *pIndex = (int) s_nWidgets;
    Add (WidgetButton, RowNext (pRow, nWidth, nState | StateFocused), pText, nState);
}

static void AddIconButton (TRow *pRow, TIconPainter Paint, unsigned nState, int *pIndex)
{
    const unsigned nSize = pRow->pTheme->M.nButtonHeight;
    *pIndex = (int) s_nWidgets;
    Add (WidgetIconButton, RowNext (pRow, nSize, nState | StateFocused), 0, nState)
        ->Paint = Paint;
}

unsigned ChooserWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_nWidgets;
}

int ChooserSelected (void)
{
    return s_nList < 0 ? -1 : s_Widgets[s_nList].nChoice;
}

void ChooserSync (TChooser *pChooser)
{
    const int nSel = ChooserSelected ();
    const TChooserVolume *p = nSel >= 0 ? &pChooser->Volumes[nSel] : 0;

    // A volume with no System cannot be a startup disk, and one that is not
    // mounted cannot be either — the controls say so by going grey rather than
    // by refusing when they are pressed.
    unsigned nStartupState = StateNormal;
    if (p == 0 || !p->bBootable || !p->bMounted)
    {
        nStartupState = StateDisabled;
    }
    if (nSel >= 0 && nSel == pChooser->nStartup)
    {
        nStartupState |= StateChecked;
    }
    s_Widgets[s_nStartup].nState = (s_Widgets[s_nStartup].nState & StateFocused)
                                 | nStartupState;

    s_Widgets[s_nReadOnly].nState = (s_Widgets[s_nReadOnly].nState & StateFocused)
                                  | (p == 0 ? StateDisabled
                                            : (p->bReadOnly ? StateChecked : StateNormal));

    // The startup volume must stay mounted, so its own tick box is held down
    // rather than left to be turned off from under it.
    unsigned nMountedState = p == 0 ? StateDisabled
                                    : (p->bMounted ? StateChecked : StateNormal);
    if (nSel >= 0 && nSel == pChooser->nStartup)
    {
        nMountedState |= StateDisabled;
    }
    s_Widgets[s_nMounted].nState = (s_Widgets[s_nMounted].nState & StateFocused)
                                 | nMountedState;

    for (unsigned i = 0; i < pChooser->nCount; i++)
    {
        RowText (pChooser, i);
        s_Items[i].nState = (int) i == pChooser->nStartup ? StateChecked : StateNormal;
    }
    DetailText (pChooser, nSel);

    // Nothing to start from is not an error to report at the last moment: the
    // button that cannot work says so before it is pressed.
    s_Widgets[s_nStart].nState = pChooser->nStartup >= 0 ? StateDefault
                                                         : StateDefault | StateDisabled;
}

TChooserAction ChooserOperate (TChooser *pChooser, int nIndex)
{
    if (nIndex == s_nStart)      return ChooserStart;
    if (nIndex == s_nSettings)   return ChooserSettings;
    if (nIndex == s_nPram)       return ChooserForgetPram;
    if (nIndex == s_nPower)      return ChooserShutDown;

    const int nSel = ChooserSelected ();
    if (nSel < 0)
    {
        ChooserSync (pChooser);
        return ChooserNothing;
    }
    TChooserVolume *p = &pChooser->Volumes[nSel];

    if (nIndex == s_nStartup)
    {
        // It behaves as one of a set and not as a switch: a machine has to
        // start from something, so choosing this one is all it can mean.
        pChooser->nStartup = nSel;
        p->bMounted = true;
    }
    else if (nIndex == s_nReadOnly)
    {
        p->bReadOnly = !p->bReadOnly;
    }
    else if (nIndex == s_nMounted)
    {
        p->bMounted = !p->bMounted;
        if (!p->bMounted && pChooser->nStartup == nSel)
        {
            pChooser->nStartup = -1;
        }
    }
    ChooserSync (pChooser);
    return ChooserNothing;
}

unsigned ChooserDiskLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                           unsigned nMax)
{
    unsigned n = 0;
    // The startup volume first. Everything else keeps the order the card gave
    // it, which is the order the user has been reading all along.
    for (int nPass = 0; nPass < 2 && n < nMax; nPass++)
    {
        for (unsigned i = 0; i < pChooser->nCount && n < nMax; i++)
        {
            const bool bFirst = (int) i == pChooser->nStartup;
            if (!pChooser->Volumes[i].bMounted || bFirst != (nPass == 0))
            {
                continue;
            }
            unsigned nAt = 0;
            if (pChooser->Volumes[i].bReadOnly)
            {
                nAt = Append (Lines[n], CHOOSER_LINE, nAt, "*");
            }
            Append (Lines[n], CHOOSER_LINE, nAt, pChooser->Volumes[i].Path);
            n++;
        }
    }
    return n;
}

// The era belongs to the icons, never to the chrome around them (§7.12) — so
// this is the one place in the firmware that reads a System version to decide
// what something looks like.
static const TGlyphImage *EraIcon (const char *pSystem)
{
    switch (pSystem[0])
    {
    case '6':   return &OkapiaIconSystem6;
    case '7':   return &OkapiaIconSystem7;
    case '8':   return &OkapiaIconMacOS8;
    case '9':   return &OkapiaIconMacOS9;
    default:    return 0;
    }
}

void ChooserDraw (TSurface *pSurface, TChooser *pChooser)
{
    TTheme Theme;
    s_nScale16 = ThemeScaleFor (pSurface->nWidth, pSurface->nHeight);
    ThemeMake (s_nScale16, &Theme);
    const TTheme *pTheme = &Theme;

    s_nWidgets = 0;

    const unsigned nDW = (unsigned) S (608);
    const unsigned nDH = (unsigned) S (458);
    s_Dialog = Rect ((int) (pSurface->nWidth  - nDW) / 2,
                     (int) (pSurface->nHeight - nDH) / 2, nDW, nDH);

    TLayout Layout;
    LayoutBegin (&Layout, pTheme, ThemeContent (s_Dialog, pTheme));

    Add (WidgetTitle, LayoutTop (&Layout, pTheme->pTitleFont->nHeight),
         Str (StrChooseSystem), StateNormal);
    LayoutRowGap (&Layout);
    Add (WidgetSeparator, LayoutTop (&Layout, 1), 0, StateNormal);
    LayoutSectionGap (&Layout);

    // The footer first, so the middle knows where it must stop.
    TRow Foot;
    RowBegin (&Foot, pTheme,
              LayoutBottom (&Layout, pTheme->M.nButtonHeight
                                     + 2 * ThemeReach (pTheme, StateDefault)));
    Foot.Free = Rect (Foot.Free.nX, Foot.Free.nY + (int) ThemeReach (pTheme, StateDefault),
                      Foot.Free.nWidth, pTheme->M.nButtonHeight);
    AddIconButton (&Foot, OkapiaPaintSettings, StateNormal, &s_nSettings);
    AddIconButton (&Foot, OkapiaPaintPram,     StateNormal, &s_nPram);
    AddIconButton (&Foot, OkapiaPaintPower,    StateNormal, &s_nPower);
    s_nStart = (int) s_nWidgets;
    Add (WidgetButton,
         RowLast (&Foot, WidgetButtonWidth (pTheme, Str (StrStart), StateDefault),
                  StateDefault | StateFocused),
         Str (StrStart), StateDefault);

    LayoutSectionGapBottom (&Layout);
    Add (WidgetSeparator, LayoutBottom (&Layout, 1), 0, StateNormal);
    LayoutSectionGapBottom (&Layout);

    // The volume's own line, then the three things one can say about it, both
    // taken off the bottom so the list gets everything that is left.
    s_nDetail = (int) s_nWidgets;
    Add (WidgetLabel, LayoutBottom (&Layout, pTheme->M.nLineHeight), s_Detail, StateNormal);
    LayoutSectionGapBottom (&Layout);

    TRect Band = LayoutBottom (&Layout, pTheme->M.nCheckSize
                                        + 2 * ThemeReach (pTheme, StateFocused));
    Band = Rect (Band.nX, Band.nY + (int) ThemeReach (pTheme, StateFocused),
                 Band.nWidth, pTheme->M.nCheckSize);
    const unsigned nGutter = pTheme->M.nGap + 2 * ThemeReach (pTheme, StateFocused);
    s_nStartup = (int) s_nWidgets;
    Add (WidgetCheckbox, RectColumn (Band, 0, 3, nGutter), Str (StrStartupDisk), StateNormal);
    s_nReadOnly = (int) s_nWidgets;
    Add (WidgetCheckbox, RectColumn (Band, 1, 3, nGutter), Str (StrReadOnly), StateNormal);
    s_nMounted = (int) s_nWidgets;
    Add (WidgetCheckbox, RectColumn (Band, 2, 3, nGutter), Str (StrMounted), StateNormal);
    LayoutSectionGapBottom (&Layout);

    // And the list takes what is left, which is the point of claiming both ends
    // first: it grows with the screen instead of being given a number.
    for (unsigned i = 0; i < pChooser->nCount && i < CHOOSER_MAX; i++)
    {
        s_Items[i].pText  = s_Rows[i];
        s_Items[i].pIcon  = EraIcon (pChooser->Volumes[i].System);
        s_Items[i].nState = StateNormal;
    }
    s_nList = (int) s_nWidgets;
    TWidget *pList = Add (WidgetList, LayoutRow (&Layout, LayoutRoom (&Layout)
                                                          - 2 * ThemeReach (pTheme,
                                                                            StateFocused),
                                                 StateFocused),
                          0, StateNormal);
    pList->pItems  = s_Items;
    pList->nItems  = pChooser->nCount;
    pList->nChoice = pChooser->nCount == 0 ? -1
                   : (pChooser->nStartup >= 0 ? pChooser->nStartup : 0);

    ChooserSync (pChooser);
    ChooserRepaint (pSurface);
}

void ChooserRepaint (TSurface *pSurface)
{
    TTheme Theme;
    ThemeMake (ThemeScaleFor (pSurface->nWidth, pSurface->nHeight), &Theme);
    Theme.DrawDesktop (pSurface, &Theme);
    Theme.DrawDialog (pSurface, s_Dialog, &Theme);
    WidgetDrawAll (pSurface, &Theme, s_Widgets, s_nWidgets);
}
