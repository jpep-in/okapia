/*
 * okapia_settings.cpp — the settings screen. See okapia_settings.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_settings.h"
#include "okapia_page.h"
#include "okapia_strings.h"
#include "okapia_datetime.h"

static const unsigned MAX_WIDGETS = 40;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

static int s_nMemory   = -1;
static int s_nMouse    = -1;
static int s_nSound    = -1;
static int s_nLanguage = -1;
static int s_nJit      = -1;
static int s_nQuickDraw = -1;
static int s_nShared   = -1;
static int s_nPath     = -1;
static int s_nName     = -1;

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

// What the pointing device reports per inch. A Macintosh assumes 200 and has no
// way to learn otherwise, so this is how it is told — and it is worth a place
// on this page rather than a line in a file on the card: correcting it means
// correcting the feel of the pointer, and nobody should have to pull the card
// and find another computer to do that.
static const int MOUSE_DPI[] = { 100, 200, 400, 600, 800, 1000 };
static const unsigned MOUSE_DPI_COUNT = sizeof MOUSE_DPI / sizeof MOUSE_DPI[0];

static char       s_MemoryText[MEMORY_COUNT][16];
static TListItem  s_MemoryItems[MEMORY_COUNT];
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

bool SettingsOverflowed (void)
{
    return s_Page.Layout.bOverflow;
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
    s_Widgets[s_nMouse].nChoice    = MouseDpiIndex (pSettings->V.nMouseDpi);
    s_Widgets[s_nSound].nChoice    = (int) pSettings->V.nSound;
    s_Widgets[s_nLanguage].nChoice = (int) pSettings->V.nLanguage;

    // The translator's box goes grey in an image built without one, and the
    // tick still says what the file holds: the next image may have it.
    unsigned nJit = pSettings->V.bJit ? StateChecked : StateNormal;
    if (!pSettings->bJitAvailable)
    {
        nJit |= StateDisabled;
    }
    WidgetSetState (&s_Widgets[s_nJit], nJit);
    WidgetSetState (&s_Widgets[s_nQuickDraw],
                    pSettings->V.bQuickDraw ? StateChecked : StateNormal);

    WidgetSetState (&s_Widgets[s_nShared],
                    pSettings->V.bShared ? StateChecked : StateNormal);

    // A volume nobody will see needs neither a place nor a name. Both fields say
    // so by going grey rather than by accepting letters that lead nowhere.
    const unsigned nShared = pSettings->V.bShared ? StateNormal : StateDisabled;
    WidgetSetState (&s_Widgets[s_nPath], nShared);
    WidgetSetState (&s_Widgets[s_nName], nShared);
}

/*
 *  The label columns
 *
 *  Measured rather than chosen: French runs longer than English almost
 *  everywhere, and a column picked against one of them is a column that
 *  truncates in the other.
 */
static unsigned Widest (const TTheme *pTheme, const TStringId *pLabels, unsigned nCount,
                        const char *pMark)
{
    unsigned nWidest = 0;
    for (unsigned i = 0; i < nCount; i++)
    {
        const unsigned n = GfxTextWidth (pTheme->pBodyFont, Str (pLabels[i]));
        nWidest = n > nWidest ? n : nWidest;
    }
    // The mark is on one label, and the widest may not be the marked one.
    return nWidest + (pMark != 0 ? GfxTextWidth (pTheme->pBodyFont, pMark) : 0)
         + pTheme->M.nGap;
}

// A label on the left of a band at the control's height, the control's room
// answered.
static TRect Labelled (TPage *pPage, const TRect &rBand, unsigned nLabel, const char *pText)
{
    const TTheme *pTheme = &pPage->Theme;
    TRow R;
    RowBegin (&R, pTheme, rBand);
    TRect L = RowNext (&R, nLabel, StateNormal);
    // On the control's own line rather than at the top of the band: a pop-up
    // is taller than a line of text, and a label aligned to the band reads as
    // belonging to whatever is above it.
    L = Rect (L.nX, L.nY + (int) (rBand.nHeight - pTheme->M.nLineHeight) / 2,
              L.nWidth, pTheme->M.nLineHeight);
    PageAdd (pPage, WidgetLabel, L, pText, StateNormal);
    return RowRest (&R, StateFocused);
}

static TWidget *Popup (TPage *pPage, const TRect &rRect, int *pnIndex,
                       TListItem *pItems, unsigned nItems)
{
    *pnIndex = (int) pPage->nCount;
    TWidget *p = PageAdd (pPage, WidgetPopup, rRect, 0, StateNormal);
    p->pItems = pItems;
    p->nItems = nItems;
    return p;
}

// The 68k engine's two speed-ups, in the half the language leaves free: a row
// of their own cost the date its room at 640x480. Two tick boxes and one note
// that says whom they are for, said once rather than after each.
static void Speedups (TPage *pPage, const TRect &rBand)
{
    const TTheme *pTheme = &pPage->Theme;
    const int nY = rBand.nY + (int) (rBand.nHeight - pTheme->M.nCheckSize) / 2;
    TRow R;
    RowBegin (&R, pTheme, rBand);
    const TStringId Ticks[2] = { StrJit, StrQdAccel };
    int *const pIndex[2] = { &s_nJit, &s_nQuickDraw };
    for (unsigned i = 0; i < 2; i++)
    {
        const unsigned nTick = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, Str (Ticks[i]));
        TRect T = RowNext (&R, nTick, StateNormal);
        T = Rect (T.nX, nY, T.nWidth, pTheme->M.nCheckSize);
        *pIndex[i] = (int) pPage->nCount;
        PageAdd (pPage, WidgetCheckbox, T, Str (Ticks[i]), StateNormal);
    }
    TRect L = RowNext (&R, GfxTextWidth (pTheme->pBodyFont, Str (StrOnly68k)), StateNormal);
    L = Rect (L.nX, rBand.nY + (int) (rBand.nHeight - pTheme->M.nLineHeight) / 2,
              L.nWidth, pTheme->M.nLineHeight);
    PageAdd (pPage, WidgetLabel, L, Str (StrOnly68k), StateNormal);
}

void SettingsDraw (TSurface *pSurface, TSettings *pSettings)
{
    BuildMenus ();
    // A page of the firmware's one window. The footer is the window's: what is
    // changed here is kept when the Macintosh starts or the board shuts down,
    // as a control panel keeps what was set in it.
    PageBeginTabbed (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, PageTabSettings);
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    const unsigned nPopup = pTheme->M.nButtonHeight;

    // The Okapia things, two to a row: five menus on five rows left no room for
    // the groups under them at 640x480. Each column measures its own labels.
    static const TStringId Left[]  = { StrMemory, StrMouseDpi, StrLanguage };
    static const TStringId Right[] = { StrSound };
    const unsigned nLeft  = Widest (pTheme, Left, 3, " *");
    const unsigned nRight = Widest (pTheme, Right, 1, 0);

    // One label carries the mark, so one buffer holds it. It has to outlive
    // this function: a component keeps the pointer it was given.
    static char Marked[48];
    StrAppend (Marked, sizeof Marked, StrAppend (Marked, sizeof Marked, 0, Str (StrMemory)), " *");

    for (unsigned nRow = 0; nRow < 3; nRow++)
    {
        const TRect Band = LayoutRow (&Layout, nPopup, StateFocused);
        const unsigned nHalf = (Band.nWidth - 2 * pTheme->M.nGap) / 2;
        const TRect A = Rect (Band.nX, Band.nY, nHalf, Band.nHeight);
        const TRect B = Rect (Band.nX + (int) (Band.nWidth - nHalf), Band.nY, nHalf, Band.nHeight);
        switch (nRow)
        {
        case 0:
            Popup (&s_Page, Labelled (&s_Page, A, nLeft, Marked), &s_nMemory,
                   s_MemoryItems, MEMORY_COUNT);
            Speedups (&s_Page, B);
            break;

        case 1:
            Popup (&s_Page, Labelled (&s_Page, A, nLeft, Str (StrMouseDpi)), &s_nMouse,
                   s_MouseItems, MOUSE_DPI_COUNT);
            Popup (&s_Page, Labelled (&s_Page, B, nRight, Str (StrSound)), &s_nSound,
                   s_SoundItems, SoundOutputCount);
            break;

        default:
            Popup (&s_Page, Labelled (&s_Page, A, nLeft, Str (StrLanguage)), &s_nLanguage,
                   s_LanguageItems, LanguageCount);
            break;
        }
        LayoutRowGap (&Layout);
    }

    LayoutSectionGap (&Layout);

    // The two groups share one label column, so their fields line up.
    // The tick boxes stand in that column too, so they count with their box.
    static const TStringId Inner[] = { StrVolumeName, StrTimeZone };
    unsigned nInner = Widest (pTheme, Inner, 2, 0);
    static const TStringId Ticks[] = { StrShare, StrNetTime };
    const unsigned nTicks = Widest (pTheme, Ticks, 2, 0) + pTheme->M.nCheckSize + pTheme->M.nGap;
    nInner = nTicks > nInner ? nTicks : nInner;

    // The shared folder: whether and where on one line, what the Macintosh
    // calls it on the next — three controls and one idea, framed as one.
    {
        TPageGroup Group;
        PageGroupBegin (&s_Page, &Group, Str (StrSharedGroup));
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nFieldHeight, StateFocused));
        const unsigned nTick = pTheme->M.nCheckSize + pTheme->M.nGap
                             + GfxTextWidth (pTheme->pBodyFont, Str (StrShare));
        TRect Tick = RowNext (&Band, nInner, StateNormal);
        // The box sits on the field's line rather than at the top of the band.
        Tick = Rect (Tick.nX,
                     Tick.nY + (int) (pTheme->M.nFieldHeight - pTheme->M.nCheckSize) / 2,
                     nTick, pTheme->M.nCheckSize);
        s_nShared = (int) s_Page.nCount;
        PageAdd (&s_Page, WidgetCheckbox, Tick, Str (StrShare), StateNormal);
        s_nPath = (int) s_Page.nCount;
        TWidget *p = PageAdd (&s_Page, WidgetField, RowRest (&Band, StateFocused),
                              pSettings->V.SharedPath, StateNormal);
        p->pEdit     = pSettings->V.SharedPath;
        p->nEditSize = SETTINGS_PATH;
        LayoutRowGap (&Layout);

        const TRect R = Labelled (&s_Page, LayoutRow (&Layout, pTheme->M.nFieldHeight, StateFocused),
                                  nInner, Str (StrVolumeName));
        s_nName = (int) s_Page.nCount;
        p = PageAdd (&s_Page, WidgetField, R, pSettings->V.SharedName, StateNormal);
        p->pEdit     = pSettings->V.SharedName;
        p->nEditSize = SETTINGS_NAME;
        PageGroupEnd (&s_Page, &Group);
    }
    LayoutRowGap (&Layout);

    DateTimeLayOut (&s_Page, &pSettings->Clock, nInner);

    SettingsSync (pSettings);
    SettingsRepaint (pSurface);
}

void SettingsRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

TSettingsAction SettingsOperate (TSettings *pSettings, int nIndex)
{
    switch (DateTimeOperate (&pSettings->Clock, nIndex))
    {
    case DateTimeNothing:
        break;

    case DateTimeHelp:
        return SettingsClockHelp;

    default:
        SettingsSync (pSettings);
        return SettingsClock;
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
            // Start changes its words, so it changes its width. Lay the page
            // out again rather than draw one label in another's button.
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
    else if (nIndex == s_nJit)
    {
        if (pSettings->bJitAvailable)
        {
            pSettings->V.bJit = !pSettings->V.bJit;
        }
    }
    else if (nIndex == s_nQuickDraw)
    {
        pSettings->V.bQuickDraw = !pSettings->V.bQuickDraw;
    }
    else if (nIndex == s_nShared)
    {
        pSettings->V.bShared = !pSettings->V.bShared;
    }

    SettingsSync (pSettings);
    return SettingsNothing;
}
