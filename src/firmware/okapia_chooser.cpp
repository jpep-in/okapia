/*
 * okapia_chooser.cpp — the screen that asks which System to start.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_chooser.h"
#include "okapia_icons.h"
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
//
// One more row than there are volumes: the first is the command that makes one.
// It is in the list rather than beside it because what it makes appears in the
// list — and it is first because the volumes below it come and go while it
// never does, so its place is the one thing on the screen that never moves.
static TListItem s_Items[CHOOSER_MAX + 1];
static char      s_Rows[CHOOSER_MAX][72];
static char      s_Detail[96];

static const int RowAction = 0;

// The list is indexed by row and everything else by volume, and the two differ
// by one. They are converted here and nowhere else: an index that means two
// things depending on where you read it is the bug this pair exists to make
// impossible.
static int VolumeOf (int nRow)    { return nRow <= RowAction ? -1 : nRow - 1; }
static int RowOf    (int nVolume) { return nVolume < 0 ? RowAction : nVolume + 1; }

// The components the rest of this file needs to reach again.
static int s_nList     = -1;
static int s_nDetail   = -1;
static int s_nMount    = -1;
static int s_nEngine   = -1;

// The two answers the emulator popup offers, in the order TChooserCPU numbers
// them minus the two that are not answers: index 0 is CPU68k, index 1 is
// CPUPowerPC.
static TListItem s_EngineItems[2];

// And the three the mount popup offers, in the order TChooserMount numbers
// them, so the index is the value and neither has to be translated into the
// other.
static TListItem s_MountItems[3];

// The two columns of the list, and what each one means. They were tick boxes
// under the list, which meant reading a row, looking down, and trusting that
// the boxes still spoke about the row one had just read. In the row they are
// the row's own answer, and the whole card can be taken in at a glance.
//
// Read-only used to be a third one. It is not a tick any more: what a volume is
// mounted as has three values, one of which sets read-only as a consequence
// rather than as an answer, and a cell can only be ticked. It went to the popup
// under the list, beside the emulator's — the same kind of question, asked the
// same way, about the same selected row.
enum { ColStartup = 1, ColMounted = 2 };
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
    // Never for a CD-ROM: nothing writes it, so a dirty bit there is something
    // whoever built the image left behind, and saying "in use" about a volume
    // that cannot be repaired — the repair writes — is worse than saying
    // nothing at all.
    if (!p->bClean && p->Mount != MountCD)
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " · ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, Str (StrVolumeInUse));
    }
    // How it is mounted, when that is not the ordinary way. The popup under the
    // list says it too, but only about the selected row: a card where one
    // volume is a CD has to say so without being clicked on.
    if (p->Mount == MountCD)
    {
        n = StrAppend (pOut, sizeof s_Rows[i], n, " · ");
        n = StrAppend (pOut, sizeof s_Rows[i], n, Str (StrMountCd));
    }
    else if (p->Mount == MountHDReadOnly)
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
    // Free space is a number one reads before writing something, so a CD-ROM is
    // asked its size instead: "0 MB free" is true of every disc ever pressed and
    // tells nobody anything. Same for the clean bit, which nothing can dirty.
    if (p->Mount == MountCD)
    {
        n = StrAppendNumber (s_Detail, sizeof s_Detail, n, p->nTotalKB / 1024);
        n = StrAppend (s_Detail, sizeof s_Detail, n, " ");
        StrAppend (s_Detail, sizeof s_Detail, n, Str (StrUnitMb));
        return;
    }
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

bool ChooserOverflowed (void)
{
    return s_Page.Layout.bOverflow;
}

// Which *volume* is selected, so the answer means the same thing everywhere. A
// selection sitting on the command row is no volume at all, and says so.
int ChooserSelected (void)
{
    return s_nList < 0 ? -1 : VolumeOf (s_Widgets[s_nList].nChoice);
}

void ChooserSettleEngines (TChooser *pChooser)
{
    // The rule in one place: a System built for one processor settles which
    // emulator starts it, and only a universal one leaves the question open.
    for (unsigned i = 0; i < pChooser->nCount; i++)
    {
        TChooserVolume *v = &pChooser->Volumes[i];
        if (v->CPU == CPU68k || v->CPU == CPUPowerPC)
        {
            v->Engine = v->CPU;
        }
        else if (v->Engine != CPU68k && v->Engine != CPUPowerPC)
        {
            // Universal with nothing remembered, or no readable System at all:
            // start with what this kernel already carries, which is the answer
            // that needs no reboot.
            v->Engine = pChooser->Built == CPUPowerPC ? CPUPowerPC : CPU68k;
        }
    }
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

void ChooserSync (TChooser *pChooser)
{
    const int nSel = ChooserSelected ();

    for (unsigned i = 0; i < pChooser->nCount; i++)
    {
        const TChooserVolume *v = &pChooser->Volumes[i];
        RowText (pChooser, i);
        TListItem *pItem = &s_Items[RowOf ((int) i)];
        pItem->nState = StateNormal;
        // The picture belongs with the words, and both are restated here rather
        // than only where the page is laid out: changing how a volume is
        // mounted does not lay the page out again, so an icon set once showed
        // a disc as a folder and a folder as a disc until the next repaint.
        //
        // A disc says what it is before it says what is on it: the era is the
        // System's, the medium is the volume's, and a row wears one picture.
        pItem->pIcon = v->Mount == MountCD ? &OkapiaIconCdrom : EraIcon (v->System);

        // A volume with no System cannot be a startup disk, and one that is not
        // mounted cannot be either — the mark says so by going grey rather than
        // by refusing when it is pressed.
        //
        // A CD-ROM can, and it is not the list order that says so: the ROM is
        // told which *driver* to start from, through the parameter RAM, which
        // is what ChooserBootDriver() answers. That is how a Macintosh started
        // from a disc, and it is the same setting the C key stood for.
        unsigned nStartup = (!v->bBootable || !v->bMounted) ? StateDisabled : StateNormal;
        if ((int) i == pChooser->nStartup)
        {
            nStartup |= StateChecked;
        }
        pItem->nCell[ColStartup - 1] = nStartup;

        // The startup volume must stay mounted, so its own mark is held down
        // rather than left to be turned off from under it.
        unsigned nMounted = v->bMounted ? StateChecked : StateNormal;
        if ((int) i == pChooser->nStartup)
        {
            nMounted |= StateDisabled;
        }
        pItem->nCell[ColMounted - 1] = nMounted;
    }
    DetailText (pChooser, nSel);

    if (s_nMount >= 0)
    {
        TWidget *pPopup = &s_Widgets[s_nMount];
        pPopup->nChoice = nSel < 0 ? 0 : (int) pChooser->Volumes[nSel].Mount;
        // Grey with no selection and never otherwise: unlike the emulator,
        // there is always something to decide here — every volume is mounted
        // one of the three ways, including the ones that are not mounted at
        // all, which is what they would be mounted as.
        const unsigned nState = nSel < 0 ? StateDisabled : StateNormal;
        WidgetSetState (pPopup, nState);
        WidgetSetState (&s_Widgets[s_nMount - 1], nState);
    }

    ChooserSettleEngines (pChooser);

    if (s_nEngine >= 0)
    {
        TWidget *pPopup = &s_Widgets[s_nEngine];
        const bool bAsk = nSel >= 0
                       && pChooser->Volumes[nSel].CPU == CPUUniversal;
        pPopup->nChoice = nSel < 0 ? 0
                        : (pChooser->Volumes[nSel].Engine == CPUPowerPC ? 1 : 0);
        // Shown either way, and greyed when there is nothing to decide: a
        // control that disappears takes its answer with it, and which emulator
        // is about to run is worth reading even when it was not a choice.
        WidgetSetState (pPopup, bAsk ? StateNormal : StateDisabled);
        WidgetSetState (&s_Widgets[s_nEngine - 1], bAsk ? StateNormal : StateDisabled);
    }

    // Nothing to start from is not an error to report at the last moment: the
    // button that cannot work says so before it is pressed.
    WidgetSetState (&s_Widgets[s_nStart], pChooser->nStartup >= 0
                                          ? StateDefault
                                          : StateDefault | StateDisabled);
}

TChooserAction ChooserOperate (TChooser *pChooser, int nIndex, unsigned nCell)
{
    if (nIndex == s_nStart)      return ChooserStart;
    if (nIndex == s_nSettings)   return ChooserSettings;
    if (nIndex == s_nInfo)       return ChooserInformation;
    if (nIndex == s_nPram)       return ChooserForgetPram;
    if (nIndex == s_nPower)      return ChooserShutDown;

    const int nSel = ChooserSelected ();

    if (nIndex == s_nMount && nSel >= 0)
    {
        TChooserVolume *v = &pChooser->Volumes[nSel];
        const int nPick = s_Widgets[s_nMount].nChoice;
        v->Mount = nPick == (int) MountCD ? MountCD
                 : (nPick == (int) MountHDReadOnly ? MountHDReadOnly : MountHD);
        ChooserSync (pChooser);
        return ChooserNothing;
    }

    if (nIndex == s_nEngine && nSel >= 0)
    {
        // Only a universal System has anything to change, and the popup is
        // already greyed otherwise; the test is here as well because a screen
        // that trusts its own greying is a screen that can be driven past it.
        TChooserVolume *v = &pChooser->Volumes[nSel];
        if (v->CPU == CPUUniversal)
        {
            v->Engine = s_Widgets[s_nEngine].nChoice == 1 ? CPUPowerPC : CPU68k;
        }
        ChooserSync (pChooser);
        return ChooserNothing;
    }

    // The command row, which is the only row that means something when the
    // click lands on the row itself rather than on one of its marks.
    if (nIndex == s_nList && nCell == 0 && s_Widgets[s_nList].nChoice == RowAction)
    {
        ChooserSync (pChooser);
        return ChooserNewVolume;
    }

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
            const TChooserVolume *v = &pChooser->Volumes[i];
            const bool bFirst = (int) i == pChooser->nStartup;
            if (!v->bMounted || v->Mount == MountCD || bFirst != (nPass == 0))
            {
                continue;
            }
            unsigned nAt = 0;
            if (v->Mount == MountHDReadOnly)
            {
                nAt = StrAppend (Lines[n], CHOOSER_LINE, nAt, "*");
            }
            StrAppend (Lines[n], CHOOSER_LINE, nAt, v->Path);
            n++;
        }
    }
    return n;
}

unsigned ChooserCdromLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                            unsigned nMax)
{
    unsigned n = 0;
    // No `*`: CDROMInit() opens every one of them read-only whatever the line
    // says (cdrom.cpp:324). The startup volume first, on the same two passes as
    // the disks — `bootdriver` names the driver and not the drive, so with two
    // discs the order is all that is left to say which of them is meant.
    for (int nPass = 0; nPass < 2 && n < nMax; nPass++)
    {
        for (unsigned i = 0; i < pChooser->nCount && n < nMax; i++)
        {
            const TChooserVolume *v = &pChooser->Volumes[i];
            const bool bFirst = (int) i == pChooser->nStartup;
            if (!v->bMounted || v->Mount != MountCD || bFirst != (nPass == 0))
            {
                continue;
            }
            StrAppend (Lines[n], CHOOSER_LINE, 0, v->Path);
            n++;
        }
    }
    return n;
}

int ChooserBootDriver (const TChooser *pChooser)
{
    if (pChooser->nStartup < 0 || pChooser->nStartup >= (int) pChooser->nCount)
    {
        return 0;
    }
    return pChooser->Volumes[pChooser->nStartup].Mount == MountCD
         ? CHOOSER_BOOT_CDROM : 0;
}

unsigned ChooserEngineLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                             unsigned nMax)
{
    unsigned n = 0;
    for (unsigned i = 0; i < pChooser->nCount && n < nMax; i++)
    {
        const TChooserVolume *v = &pChooser->Volumes[i];
        // Only where there was a question. Writing a line for a System that
        // can only run one way stores an answer that the next System on that
        // path would contradict, and the file would then be wrong rather than
        // merely redundant.
        if (v->CPU != CPUUniversal)
        {
            continue;
        }
        unsigned nAt = StrAppend (Lines[n], CHOOSER_LINE, 0, v->Path);
        nAt = StrAppend (Lines[n], CHOOSER_LINE, nAt, " ");
        StrAppend (Lines[n], CHOOSER_LINE, nAt,
                   v->Engine == CPUPowerPC ? "powerpc" : "68k");
        n++;
    }
    return n;
}

TChooserCPU ChooserStartupEngine (const TChooser *pChooser)
{
    if (pChooser->nStartup < 0 || pChooser->nStartup >= (int) pChooser->nCount)
    {
        return pChooser->Built;
    }
    const TChooserVolume *v = &pChooser->Volumes[pChooser->nStartup];
    return v->Engine == CPUPowerPC ? CPUPowerPC : CPU68k;
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
    // The keyboard starts here, and ScreenInit obeys a screen that says so.
    // Start is what one came to press: left to "the first control that can hold
    // it" the focus began on the settings mark in the footer, and Return acts
    // on what the keyboard holds.
    s_nStart    = PageLast (&s_Page, Str (StrStart), StateDefault | StateFocused);

    // The volume's own line, then the three things one can say about it, both
    // taken off the bottom so the list gets everything that is left.
    s_nDetail = (int) s_Page.nCount;
    Add (WidgetLabel, LayoutRowBottom (&Layout, pTheme->M.nLineHeight, StateNormal),
         s_Detail, StateNormal);
    LayoutSectionGapBottom (&Layout);

    // Which emulator starts the selected volume. On the main screen and not
    // behind the settings, for the same reason the information button is: it
    // belongs to the volume one is looking at, and a page reached in two clicks
    // is a page nobody opens at that moment.
    s_EngineItems[0].pText = Str (StrEmulator68k);
    s_EngineItems[1].pText = Str (StrEmulatorPowerpc);
    // The label first and the popup taking what is left: the answers are long
    // in every language, and a fixed width for them is a width that one
    // translation overruns. The wider of the two labels sets both, so the two
    // popups line up — measured per language rather than chosen, because which
    // of "Emulator" and "Mount as" is the longer is not the same question in
    // English and in French.
    const unsigned nWidthEngine = GfxTextWidth (pTheme->pBodyFont, Str (StrEmulator));
    const unsigned nWidthMount  = GfxTextWidth (pTheme->pBodyFont, Str (StrMountAs));
    const unsigned nLabel = nWidthEngine > nWidthMount ? nWidthEngine : nWidthMount;
    {
        TRow Band;
        RowBegin (&Band, pTheme,
                  LayoutRowBottom (&Layout, pTheme->M.nButtonHeight, StateFocused));
        Add (WidgetLabel, RowNext (&Band, nLabel, StateNormal),
             Str (StrEmulator), StateNormal);
        s_nEngine = (int) s_Page.nCount;
        TWidget *pPopup = PageAdd (&s_Page, WidgetPopup, RowRest (&Band, StateFocused),
                                   0, StateNormal);
        pPopup->pItems = s_EngineItems;
        pPopup->nItems = 2;
    }
    LayoutRowGapBottom (&Layout);

    // And how it is handed to the Macintosh. The same question asked the same
    // way, about the same selected volume, so it belongs beside the emulator's
    // and not in a column: its three answers are the whole vocabulary the
    // preferences have for one volume, and no tick box holds three.
    s_MountItems[MountHD].pText         = Str (StrMountHd);
    s_MountItems[MountHDReadOnly].pText = Str (StrMountHdReadOnly);
    s_MountItems[MountCD].pText         = Str (StrMountCd);
    {
        TRow Band;
        RowBegin (&Band, pTheme,
                  LayoutRowBottom (&Layout, pTheme->M.nButtonHeight, StateFocused));
        Add (WidgetLabel, RowNext (&Band, nLabel, StateNormal),
             Str (StrMountAs), StateNormal);
        s_nMount = (int) s_Page.nCount;
        TWidget *pPopup = PageAdd (&s_Page, WidgetPopup, RowRest (&Band, StateFocused),
                                   0, StateNormal);
        pPopup->pItems = s_MountItems;
        pPopup->nItems = 3;
    }
    LayoutSectionGapBottom (&Layout);

    // And the list takes what is left, which is the point of claiming both ends
    // first: it grows with the screen instead of being given a number.

    s_Items[RowAction].pText   = Str (StrNewVolume);
    s_Items[RowAction].pIcon   = 0;
    s_Items[RowAction].nState  = StateNormal;
    s_Items[RowAction].bAction = true;
    for (unsigned i = 0; i < pChooser->nCount && i < CHOOSER_MAX; i++)
    {
        TListItem *pItem = &s_Items[RowOf ((int) i)];
        pItem->pText   = s_Rows[i];
        // The picture is ChooserSync's, which runs at the end of this and again
        // every time the model changes; here only what the layout owns.
        pItem->nState  = StateNormal;
        pItem->bAction = false;
    }
    s_Columns[ColStartup - 1].pHeader = Str (StrStartupDisk);
    s_Columns[ColStartup - 1].bRadio  = true;
    s_Columns[ColMounted - 1].pHeader = Str (StrMounted);
    s_Columns[ColMounted - 1].bRadio  = false;

    s_nList = (int) s_Page.nCount;
    TWidget *pList = Add (WidgetList, LayoutRow (&Layout, LayoutRoom (&Layout)
                                                          - 2 * ThemeReach (pTheme,
                                                                            StateFocused),
                                                 StateFocused),
                          0, StateNormal);
    pList->pItems   = s_Items;
    pList->pColumns = s_Columns;
    pList->nColumns = LIST_COLUMNS;
    pList->nItems   = pChooser->nCount + 1;
    // The startup volume, or the first one — and on a card with no volume at
    // all, the only row there is, which is the one that makes one.
    pList->nChoice  = pChooser->nCount == 0 ? RowAction
                    : RowOf (pChooser->nStartup >= 0 ? pChooser->nStartup : 0);
    // The keyboard starts on the name, where reading starts.
    pList->nCell    = 0;

    ChooserSync (pChooser);
    ChooserRepaint (pSurface);
}

void ChooserRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}
