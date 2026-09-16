/*
 * okapia_settings.cpp — the settings screen. See okapia_settings.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_settings.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 24;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

static int s_nMemory   = -1;
static int s_nRefresh  = -1;
static int s_nMouse    = -1;
static int s_nSound    = -1;
static int s_nLanguage = -1;
static int s_nBootMenu = -1;
static int s_nShared   = -1;
static int s_nPath     = -1;
static int s_nName     = -1;
static int s_nNote     = -1;
static int s_nBack     = -1;
static int s_nSave     = -1;

/*
 *  The menus
 *
 *  Their text is built once per layout rather than pointed at, because most of
 *  it is a number with a unit after it and there is nowhere else for that to
 *  live. The values beside them are the code's, not the interface's: what the
 *  preferences file carries is a frame count, and "Dynamic" is what zero means.
 */
static const unsigned MEMORY_MB[]  = { 64, 128, 256 };
static const unsigned MEMORY_COUNT = sizeof MEMORY_MB / sizeof MEMORY_MB[0];

// The rungs of Basilisk's own Window Refresh Rate menu (video_x.cpp), which is
// what the preference means: 60 Hz is one VBL per frame, 5 Hz is twelve. The
// rate is written out rather than divided, because 7.5 is not an integer and a
// menu that says "7 Hz" would be describing something else.
static const struct { int nSkip; const char *pRate; } REFRESH[] =
{
    { 1, "60" }, { 2, "30" }, { 4, "15" }, { 6, "10" }, { 8, "7.5" }, { 12, "5" }
};
static const unsigned REFRESH_COUNT = sizeof REFRESH / sizeof REFRESH[0];

// What the pointing device reports per inch. A Macintosh assumes 200 and has no
// way to learn otherwise, so this is how it is told — and it is worth a place
// on this page rather than a line in a file on the card: correcting it means
// correcting the feel of the pointer, and nobody should have to pull the card
// and find another computer to do that.
static const int MOUSE_DPI[] = { 100, 200, 400, 600, 800, 1000 };
static const unsigned MOUSE_DPI_COUNT = sizeof MOUSE_DPI / sizeof MOUSE_DPI[0];

static char       s_MemoryText[MEMORY_COUNT][16];
static TListItem  s_MemoryItems[MEMORY_COUNT];
static char       s_RefreshText[REFRESH_COUNT + 1][16];
static TListItem  s_RefreshItems[REFRESH_COUNT + 1];
static char       s_MouseText[MOUSE_DPI_COUNT][16];
static TListItem  s_MouseItems[MOUSE_DPI_COUNT];
static TListItem  s_SoundItems[SoundOutputCount];
static TListItem  s_LanguageItems[LanguageCount];

// The nearest offered value, so a card carrying something we do not list — set
// by hand, or by a later version — comes back as the closest thing on the menu
// rather than as the first entry.
static int MouseDpiIndex (int nDpi)
{
    int nBest = 0;
    for (unsigned i = 1; i < MOUSE_DPI_COUNT; i++)
    {
        const int a = MOUSE_DPI[i]     > nDpi ? MOUSE_DPI[i]     - nDpi : nDpi - MOUSE_DPI[i];
        const int b = MOUSE_DPI[nBest] > nDpi ? MOUSE_DPI[nBest] - nDpi : nDpi - MOUSE_DPI[nBest];
        if (a < b)
        {
            nBest = (int) i;
        }
    }
    return nBest;
}

static void BuildMenus (void)
{
    for (unsigned i = 0; i < MEMORY_COUNT; i++)
    {
        unsigned n = StrAppendNumber (s_MemoryText[i], sizeof s_MemoryText[i], 0,
                                      MEMORY_MB[i]);
        n = StrAppend (s_MemoryText[i], sizeof s_MemoryText[i], n, " ");
        StrAppend (s_MemoryText[i], sizeof s_MemoryText[i], n, Str (StrUnitMb));
        s_MemoryItems[i].pText  = s_MemoryText[i];
        s_MemoryItems[i].pIcon  = 0;
        s_MemoryItems[i].nState = StateNormal;
    }

    // Dynamic first, because it is the default and because it is the only entry
    // that is not a rate: it holds the compositor to a fraction of wall time
    // instead of to a number of hertz.
    StrAppend (s_RefreshText[0], sizeof s_RefreshText[0], 0, Str (StrDynamic));
    s_RefreshItems[0].pText  = s_RefreshText[0];
    s_RefreshItems[0].pIcon  = 0;
    s_RefreshItems[0].nState = StateNormal;
    for (unsigned i = 0; i < REFRESH_COUNT; i++)
    {
        unsigned n = StrAppend (s_RefreshText[i + 1], sizeof s_RefreshText[i + 1], 0,
                                REFRESH[i].pRate);
        n = StrAppend (s_RefreshText[i + 1], sizeof s_RefreshText[i + 1], n, " ");
        StrAppend (s_RefreshText[i + 1], sizeof s_RefreshText[i + 1], n, Str (StrUnitHz));
        s_RefreshItems[i + 1].pText  = s_RefreshText[i + 1];
        s_RefreshItems[i + 1].pIcon  = 0;
        s_RefreshItems[i + 1].nState = StateNormal;
    }

    // The unit is in the value rather than the label, so the row stays short in
    // every language and the number carries its own meaning.
    for (unsigned i = 0; i < MOUSE_DPI_COUNT; i++)
    {
        unsigned n = StrAppendNumber (s_MouseText[i], sizeof s_MouseText[i], 0,
                                      MOUSE_DPI[i]);
        StrAppend (s_MouseText[i], sizeof s_MouseText[i], n, " dpi");
        s_MouseItems[i].pText  = s_MouseText[i];
        s_MouseItems[i].pIcon  = 0;
        s_MouseItems[i].nState = StateNormal;
    }

    static const TStringId SoundNames[SoundOutputCount] =
    {
        StrSoundOff, StrSoundHdmi, StrSoundJack, StrSoundUsb
    };
    for (unsigned i = 0; i < SoundOutputCount; i++)
    {
        s_SoundItems[i].pText  = Str (SoundNames[i]);
        s_SoundItems[i].pIcon  = 0;
        s_SoundItems[i].nState = StateNormal;
    }

    // Each language in its own tongue, which is how every list of languages
    // that has ever been useful is written. Straight from the table rather than
    // through Str(), since Str() only ever answers in the one now chosen.
    for (unsigned i = 0; i < LanguageCount; i++)
    {
        s_LanguageItems[i].pText  = OkapiaStringTable[i][StrLanguageName];
        s_LanguageItems[i].pIcon  = 0;
        s_LanguageItems[i].nState = StateNormal;
    }
}

static int MemoryIndex (unsigned nMB)
{
    for (unsigned i = 0; i < MEMORY_COUNT; i++)
    {
        if (MEMORY_MB[i] == nMB)
        {
            return (int) i;
        }
    }
    return (int) MEMORY_COUNT - 1;
}

unsigned SettingsMemoryOffered (unsigned nMB)
{
    return MEMORY_MB[MemoryIndex (nMB)];
}

static int RefreshIndex (int nSkip)
{
    for (unsigned i = 0; i < REFRESH_COUNT; i++)
    {
        if (REFRESH[i].nSkip == nSkip)
        {
            return (int) i + 1;
        }
    }
    return 0;                           // anything else reads as Dynamic
}

bool SettingsNeedsRestart (const TSettings *pSettings)
{
    // Only the memory. Everything else on this screen is read when the
    // Macintosh starts — which it does every time the chooser's button is
    // pressed — whereas the 257 MB block is claimed in CKernel::Initialize(),
    // before any of this runs, because Circle serves large blocks by walking
    // forward through free space and a late request fails on a 1 GB board.
    return pSettings->V.nMemoryMB != pSettings->Opened.nMemoryMB;
}

unsigned SettingsWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

TRect SettingsDialog (void)
{
    return s_Page.Dialog;
}

void SettingsSync (TSettings *pSettings)
{
    const unsigned nHave = pSettings->nSoundAvailable == 0 ? ~0u
                                                           : pSettings->nSoundAvailable;
    for (unsigned i = 0; i < SoundOutputCount; i++)
    {
        s_SoundItems[i].nState = (nHave & (1u << i)) != 0 ? StateNormal : StateDisabled;
    }

    s_Widgets[s_nMemory].nChoice   = MemoryIndex (pSettings->V.nMemoryMB);
    s_Widgets[s_nRefresh].nChoice  = RefreshIndex (pSettings->V.nFrameSkip);
    s_Widgets[s_nMouse].nChoice    = MouseDpiIndex (pSettings->V.nMouseDpi);
    s_Widgets[s_nSound].nChoice    = (int) pSettings->V.nSound;
    s_Widgets[s_nLanguage].nChoice = (int) pSettings->V.nLanguage;

    WidgetSetState (&s_Widgets[s_nBootMenu],
                    pSettings->V.bBootMenu ? StateChecked : StateNormal);
    WidgetSetState (&s_Widgets[s_nShared],
                    pSettings->V.bShared ? StateChecked : StateNormal);

    // A volume nobody will see needs neither a place nor a name. Both fields say
    // so by going grey rather than by accepting letters that lead nowhere.
    const unsigned nShared = pSettings->V.bShared ? StateNormal : StateDisabled;
    WidgetSetState (&s_Widgets[s_nPath], nShared);
    WidgetSetState (&s_Widgets[s_nName], nShared);

    // The note lights up when it becomes true, instead of standing there
    // explaining a mark nobody has earned yet.
    WidgetSetState (&s_Widgets[s_nNote],
                    SettingsNeedsRestart (pSettings) ? StateNormal : StateDisabled);
}

/*
 *  One row: a label on the left, its control on the right.
 *
 *  The label column is as wide as the widest label and no wider, measured here
 *  rather than chosen: French runs longer than English almost everywhere, and a
 *  column picked against one of them is a column that truncates in the other.
 */
static unsigned LabelWidth (const TTheme *pTheme)
{
    static const TStringId Labels[] =
    {
        StrMemory, StrScreenRefresh, StrSound, StrLanguage, StrVolumeName
    };
    unsigned nWidest = 0;
    for (unsigned i = 0; i < sizeof Labels / sizeof Labels[0]; i++)
    {
        const unsigned n = GfxTextWidth (pTheme->pBodyFont, Str (Labels[i]));
        if (n > nWidest)
        {
            nWidest = n;
        }
    }
    // The mark is on the label, and the widest label may not be the marked one.
    return nWidest + GfxTextWidth (pTheme->pBodyFont, " *");
}

static TRect Row (TPage *pPage, unsigned nHeight, unsigned nLabelWidth,
                  TStringId Label, bool bMark)
{
    const TTheme *pTheme = &pPage->Theme;
    TRow R;
    RowBegin (&R, pTheme, LayoutRow (&pPage->Layout, nHeight, StateFocused));

    // One label carries the mark, so one buffer holds it. It has to outlive this
    // function: a component keeps the pointer it was given and is redrawn from
    // it long after the layout has finished.
    static char Marked[48];
    const char *pText = Str (Label);
    if (bMark)
    {
        const unsigned n = StrAppend (Marked, sizeof Marked, 0, pText);
        StrAppend (Marked, sizeof Marked, n, " *");
        pText = Marked;
    }

    TRect Label_ = RowNext (&R, nLabelWidth, StateNormal);
    // The label sits on the control's own line rather than at the top of the
    // band: a pop-up is taller than a line of text, and a label aligned to the
    // band reads as belonging to whatever is above it.
    Label_ = Rect (Label_.nX, Label_.nY + (int) (nHeight - pTheme->M.nLineHeight) / 2,
                   Label_.nWidth, pTheme->M.nLineHeight);
    PageAdd (pPage, WidgetLabel, Label_, pText, StateNormal);
    return RowRest (&R, StateFocused);
}

void SettingsDraw (TSurface *pSurface, TSettings *pSettings)
{
    BuildMenus ();
    // The firmware's one window. Opening the settings is turning a page, not
    // opening another dialogue, and a frame that changes size between the two
    // is what makes it read as the second.
    PageBegin (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, PAGE_WIDTH, PAGE_HEIGHT,
               Str (StrSettingsTitle));
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    // The footer first, so the middle knows where it must stop.
    // Laid out with the label it will actually wear, not with the longest one it
    // could: a button sized for "Save and restart" and drawn saying "Save" is a
    // button with a hole in it. Changing which it says lays the page out again,
    // which is what SettingsRelayout is for.
    PageFooter (&s_Page);
    s_nSave = PageLast (&s_Page, SettingsNeedsRestart (pSettings) ? Str (StrSaveRestart)
                                                                  : Str (StrSave),
                        StateDefault);
    s_nBack = PageLast (&s_Page, Str (StrCancel), StateNormal);
    // In the footer beside the buttons, because it is a note about the assent:
    // above the rule it floated under the last control and read as belonging to
    // it instead.
    s_nNote = PageNote (&s_Page, Str (StrRestartRequired), StateDisabled);

    const unsigned nLabel = LabelWidth (pTheme);
    const unsigned nPopup = pTheme->M.nButtonHeight;

    // The rectangle first, then the index, then the component. Row() adds the
    // label itself, and an argument is evaluated before the call it is an
    // argument to — so an index taken on the line above named the label and not
    // the control, and every menu on this page answered for its neighbour.
    TRect R = Row (&s_Page, nPopup, nLabel, StrMemory, true);
    s_nMemory = (int) s_Page.nCount;
    TWidget *p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
    p->pItems = s_MemoryItems;
    p->nItems = MEMORY_COUNT;
    LayoutRowGap (&Layout);

    R = Row (&s_Page, nPopup, nLabel, StrScreenRefresh, false);
    s_nRefresh = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
    p->pItems = s_RefreshItems;
    p->nItems = REFRESH_COUNT + 1;
    LayoutRowGap (&Layout);

    R = Row (&s_Page, nPopup, nLabel, StrMouseDpi, false);
    s_nMouse = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
    p->pItems = s_MouseItems;
    p->nItems = MOUSE_DPI_COUNT;
    LayoutRowGap (&Layout);

    R = Row (&s_Page, nPopup, nLabel, StrSound, false);
    s_nSound = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
    p->pItems = s_SoundItems;
    p->nItems = SoundOutputCount;
    LayoutRowGap (&Layout);

    R = Row (&s_Page, nPopup, nLabel, StrLanguage, false);
    s_nLanguage = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetPopup, R, 0, StateNormal);
    p->pItems = s_LanguageItems;
    p->nItems = LanguageCount;
    LayoutRowGap (&Layout);

    // On its own line and full width: the label is a sentence, not a noun, and
    // a sentence squeezed into the column the other labels share would be cut.
    // It belongs to this group rather than to the shared folder below — both
    // are about Okapia and not about the Macintosh, and the folder's three
    // controls are one idea that a fourth would join by accident.
    {
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nCheckSize, StateFocused));
        const unsigned nWide = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, Str (StrBootMenu));
        s_nBootMenu = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetCheckbox, RowNext (&Band, nWide, StateFocused),
                 Str (StrBootMenu), StateNormal);
    }
    LayoutSectionGap (&Layout);

    // The shared folder is three controls and one idea, so they sit together with
    // no section between them: the tick box says whether there is a volume, the
    // field beside it says where it is on the card, and the row below says what
    // the Mac calls it. Whether and where belong on one line — turning it off
    // and reading the path it would have used is one glance, not two.
    TRow Band;
    RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nFieldHeight, StateFocused));
    const unsigned nTick = pTheme->M.nCheckSize + pTheme->M.nGap
                         + GfxTextWidth (pTheme->pBodyFont, Str (StrSharedFolder));
    TRect Tick = RowNext (&Band, nTick, StateFocused);
    // The box sits on the field's line rather than at the top of the band: a
    // field is taller than a tick box, and a box aligned to the band reads as
    // belonging to the row above.
    Tick = Rect (Tick.nX,
                 Tick.nY + (int) (pTheme->M.nFieldHeight - pTheme->M.nCheckSize) / 2,
                 Tick.nWidth, pTheme->M.nCheckSize);
    s_nShared = (int) s_Page.nCount;
    PageAdd (&s_Page, WidgetCheckbox, Tick, Str (StrSharedFolder), StateNormal);

    R = RowRest (&Band, StateFocused);
    s_nPath = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetField, R, pSettings->V.SharedPath, StateNormal);
    p->pEdit     = pSettings->V.SharedPath;
    p->nEditSize = SETTINGS_PATH;
    LayoutRowGap (&Layout);

    R = Row (&s_Page, pTheme->M.nFieldHeight, nLabel, StrVolumeName, false);
    s_nName = (int) s_Page.nCount;
    p = PageAdd (&s_Page, WidgetField, R, pSettings->V.SharedName, StateNormal);
    p->pEdit     = pSettings->V.SharedName;
    p->nEditSize = SETTINGS_NAME;

    SettingsSync (pSettings);
    SettingsRepaint (pSurface);
}

void SettingsRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

TSettingsAction SettingsOperate (TSettings *pSettings, int nIndex)
{
    if (nIndex == s_nBack)  return SettingsBack;
    if (nIndex == s_nSave)
    {
        return SettingsNeedsRestart (pSettings) ? SettingsSaveRestart : SettingsSave;
    }

    if (nIndex == s_nMemory)
    {
        const int n = s_Widgets[s_nMemory].nChoice;
        const bool bWas = SettingsNeedsRestart (pSettings);
        if (n >= 0 && n < (int) MEMORY_COUNT)
        {
            pSettings->V.nMemoryMB = MEMORY_MB[n];
        }
        if (SettingsNeedsRestart (pSettings) != bWas)
        {
            // The assent changes its words, so it changes its width. Lay the
            // page out again rather than draw one label in another's button.
            SettingsSync (pSettings);
            return SettingsRelayout;
        }
    }
    else if (nIndex == s_nMouse)
    {
        const int n = s_Widgets[s_nMouse].nChoice;
        if (n >= 0 && n < (int) MOUSE_DPI_COUNT)
        {
            pSettings->V.nMouseDpi = MOUSE_DPI[n];
        }
    }
    else if (nIndex == s_nRefresh)
    {
        const int n = s_Widgets[s_nRefresh].nChoice;
        pSettings->V.nFrameSkip = n <= 0 ? 0 : REFRESH[n - 1].nSkip;
    }
    else if (nIndex == s_nSound)
    {
        const int n = s_Widgets[s_nSound].nChoice;
        if (n >= 0 && n < (int) SoundOutputCount)
        {
            pSettings->V.nSound = (unsigned) n;
        }
    }
    else if (nIndex == s_nLanguage)
    {
        const int n = s_Widgets[s_nLanguage].nChoice;
        if (n >= 0 && n < (int) LanguageCount && (unsigned) n != pSettings->V.nLanguage)
        {
            pSettings->V.nLanguage = (unsigned) n;
            // At once, and the page laid out again: a language one has to save
            // and come back to see is a language nobody can check they picked.
            StringsSetLanguage ((TLanguage) n);
            return SettingsRelayout;
        }
    }
    else if (nIndex == s_nBootMenu)
    {
        pSettings->V.bBootMenu = !pSettings->V.bBootMenu;
    }
    else if (nIndex == s_nShared)
    {
        pSettings->V.bShared = !pSettings->V.bShared;
    }

    SettingsSync (pSettings);
    return SettingsNothing;
}
