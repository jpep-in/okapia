/*
 * okapia_chooser.cpp — the screen that asks which System to start.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_chooser.h"
#include "okapia_page.h"
#include "okapia_strings.h"

// The screen's own array. Laid out once and then only restated, so that the
// list keeps pointing at the same rows while the user works.
static const unsigned MAX_WIDGETS = 24;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

// What each row says, and what the selected volume's line says underneath. Both
// are rebuilt in place, so nothing has to be laid out again when a tick box
// changes what a volume is.
static TListItem s_Items[CHOOSER_MAX];
static char      s_Rows[CHOOSER_MAX][72];
static char      s_Detail[96];

// The components the rest of this file needs to reach again.
static int s_nList     = -1;
static int s_nDetail   = -1;

// The three columns of the list, and what each one means. They were three tick
// boxes under the list, which meant reading a row, looking down, and trusting
// that the boxes still spoke about the row one had just read. In the row they
// are the row's own answer, and the whole card can be taken in at a glance.
enum { ColStartup = 1, ColReadOnly = 2, ColMounted = 3 };
static TListColumn s_Columns[LIST_COLUMNS];
static int s_nStart    = -1;
static int s_nSettings = -1;
static int s_nInfo     = -1;
static int s_nPram     = -1;
static int s_nPower    = -1;

// "Macintosh HD — System 7.1.2" — and what is wrong with it, when something is.
// The state belongs on the row rather than only under the selection: a card
// where one volume is in use has to say so without being clicked on.
static void RowText (const TChooser *pChooser, unsigned i)
{
    const TChooserVolume *p = &pChooser->Volumes[i];
    char *pOut = s_Rows[i];
    unsigned n = StrAppend (pOut, sizeof s_Rows[i], 0, p->Name);

    if (p->System[0] != '\0')
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " — ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, "System ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, p->System);

        // The processor belongs with the version and not among the dotted
        // states that follow: it says what this volume *is*, where those say
        // what is wrong with it. It is also what will one day say which of two
        // emulators is about to start, so it reads better beside the System
        // than at the end of a list of complaints.
        static const TStringId Chip[] =
            { StrCpu68k, StrCpu68k, StrCpuPowerpc, StrCpuUniversal };
        if (p->CPU != CPUUnknown)
        {
            n = StrAppend (pOut, sizeof s_Rows[i], n, ", ");
            n = StrAppend (pOut, sizeof s_Rows[i], n, Str (Chip[p->CPU]));
        }
    }
    else
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " — ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, Str (StrNoSystem));
    }
    if (!p->bClean)
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " · ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, Str (StrVolumeInUse));
    }
    if (p->bReadOnly)
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " · ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, Str (StrReadOnly));
    }
    if (!p->bMounted)
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " · ");
        StrAppend (pOut, sizeof s_Rows[i], n, Str (StrNotMounted));
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
    unsigned n = StrAppend (s_Detail, sizeof s_Detail, 0, p->Path);
    n = StrAppend (s_Detail, sizeof s_Detail, n, " · ");
    n = StrAppendNumber (s_Detail, sizeof s_Detail, n, p->nFreeKB / 1024);
    n = StrAppend (s_Detail, sizeof s_Detail, n, " ");
    n = StrAppend (s_Detail, sizeof s_Detail, n, Str (StrMbFree));
    n = StrAppend (s_Detail, sizeof s_Detail, n, " · ");
    StrAppend (s_Detail, sizeof s_Detail, n, p->bClean ? Str (StrVolumeClean)
                                                    : Str (StrVolumeInUse));
}

static TWidget *Add (TWidgetType Type, const TRect &rRect, const char *pText, unsigned nState)
{
    return PageAdd (&s_Page, Type, rRect, pText, nState);
}

unsigned ChooserWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

int ChooserSelected (void)
{
    return s_nList < 0 ? -1 : s_Widgets[s_nList].nChoice;
}

void ChooserSync (TChooser *pChooser)
{
    const int nSel = ChooserSelected ();

    for (unsigned i = 0; i < pChooser->nCount; i++)
    {
        const TChooserVolume *v = &pChooser->Volumes[i];
        RowText (pChooser, i);
        s_Items[i].nState = StateNormal;

        // A volume with no System cannot be a startup disk, and one that is not
        // mounted cannot be either — the mark says so by going grey rather than
        // by refusing when it is pressed.
        unsigned nStartup = (!v->bBootable || !v->bMounted) ? StateDisabled : StateNormal;
        if ((int) i == pChooser->nStartup)
        {
            nStartup |= StateChecked;
        }
        s_Items[i].nCell[ColStartup - 1] = nStartup;

        s_Items[i].nCell[ColReadOnly - 1] = v->bReadOnly ? StateChecked : StateNormal;

        // The startup volume must stay mounted, so its own mark is held down
        // rather than left to be turned off from under it.
        unsigned nMounted = v->bMounted ? StateChecked : StateNormal;
        if ((int) i == pChooser->nStartup)
        {
            nMounted |= StateDisabled;
        }
        s_Items[i].nCell[ColMounted - 1] = nMounted;
    }
    DetailText (pChooser, nSel);

    // Nothing to start from is not an error to report at the last moment: the
    // button that cannot work says so before it is pressed.
    s_Widgets[s_nStart].nState = pChooser->nStartup >= 0 ? StateDefault
                                                         : StateDefault | StateDisabled;
}

TChooserAction ChooserOperate (TChooser *pChooser, int nIndex, unsigned nCell)
{
    if (nIndex == s_nStart)      return ChooserStart;
    if (nIndex == s_nSettings)   return ChooserSettings;
    if (nIndex == s_nInfo)       return ChooserInformation;
    if (nIndex == s_nPram)       return ChooserForgetPram;
    if (nIndex == s_nPower)      return ChooserShutDown;

    const int nSel = ChooserSelected ();
    if (nIndex != s_nList || nCell == 0 || nSel < 0)
    {
        ChooserSync (pChooser);
        return ChooserNothing;
    }
    TChooserVolume *p = &pChooser->Volumes[nSel];

    if (nCell == ColStartup)
    {
        // It behaves as one of a set and not as a switch: a machine has to
        // start from something, so choosing this one is all it can mean.
        pChooser->nStartup = nSel;
        p->bMounted = true;
    }
    else if (nCell == ColReadOnly)
    {
        p->bReadOnly = !p->bReadOnly;
    }
    else if (nCell == ColMounted)
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
                nAt = StrAppend (Lines[n], CHOOSER_LINE, nAt, "*");
            }
            StrAppend (Lines[n], CHOOSER_LINE, nAt, pChooser->Volumes[i].Path);
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
    PageBegin (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, PAGE_WIDTH, PAGE_HEIGHT,
               Str (StrChooseSystem));
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    // The footer first, so the middle knows where it must stop.
    PageFooter (&s_Page);
    s_nSettings = PageIconButton (&s_Page, OkapiaPaintSettings, StateNormal);
    s_nInfo     = PageIconButton (&s_Page, OkapiaPaintInfo,     StateNormal);
    s_nPram     = PageIconButton (&s_Page, OkapiaPaintPram,     StateNormal);
    s_nPower    = PageIconButton (&s_Page, OkapiaPaintPower,    StateNormal);
    s_nStart    = PageLast (&s_Page, Str (StrStart), StateDefault);

    // The volume's own line, then the three things one can say about it, both
    // taken off the bottom so the list gets everything that is left.
    s_nDetail = (int) s_Page.nCount;
    Add (WidgetLabel, LayoutBottom (&Layout, pTheme->M.nLineHeight), s_Detail, StateNormal);
    LayoutSectionGapBottom (&Layout);

    // And the list takes what is left, which is the point of claiming both ends
    // first: it grows with the screen instead of being given a number.
    for (unsigned i = 0; i < pChooser->nCount && i < CHOOSER_MAX; i++)
    {
        s_Items[i].pText  = s_Rows[i];
        s_Items[i].pIcon  = EraIcon (pChooser->Volumes[i].System);
        s_Items[i].nState = StateNormal;
    }
    s_Columns[ColStartup  - 1].pHeader = Str (StrStartupDisk);
    s_Columns[ColStartup  - 1].bRadio  = true;
    s_Columns[ColReadOnly - 1].pHeader = Str (StrReadOnly);
    s_Columns[ColReadOnly - 1].bRadio  = false;
    s_Columns[ColMounted  - 1].pHeader = Str (StrMounted);
    s_Columns[ColMounted  - 1].bRadio  = false;

    s_nList = (int) s_Page.nCount;
    TWidget *pList = Add (WidgetList, LayoutRow (&Layout, LayoutRoom (&Layout)
                                                          - 2 * ThemeReach (pTheme,
                                                                            StateFocused),
                                                 StateFocused),
                          0, StateNormal);
    pList->pItems   = s_Items;
    pList->pColumns = s_Columns;
    pList->nColumns = LIST_COLUMNS;
    pList->nItems   = pChooser->nCount;
    pList->nChoice  = pChooser->nCount == 0 ? -1
                    : (pChooser->nStartup >= 0 ? pChooser->nStartup : 0);
    // The keyboard starts on the name, where reading starts.
    pList->nCell    = 0;

    ChooserSync (pChooser);
    ChooserRepaint (pSurface);
}

void ChooserRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}
