/*
 * okapia_newvolume.cpp — see okapia_newvolume.h.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_newvolume.h"
#include "okapia_layout.h"
#include "okapia_page.h"
#include "okapia_strings.h"

static const unsigned MAX_WIDGETS = 12;
static TWidget s_Widgets[MAX_WIDGETS];
static TPage   s_Page;

static int s_nName   = -1;
static int s_nSize   = -1;
static int s_nCreate = -1;
static int s_nCancel = -1;

// The sizes as the popup shows them, and the note under the buttons. Both are
// rebuilt in place and both outlive the layout: a component keeps the pointer
// it was given and is redrawn from it long after the layout has finished.
static TListItem s_SizeItems[NEWVOLUME_SIZES];
static char      s_SizeText[NEWVOLUME_SIZES][16];
static char      s_Note[64];

// It interrupts, so it is allowed to be its own shape — and it asks two things,
// so it is the width of a question and not of a page. The height is the two
// rows plus the bands around them and nothing spare: at 184 the checks go red
// in French, which is the measurement that set it rather than a guess.
static const unsigned SHEET_WIDTH  = 460;
static const unsigned SHEET_HEIGHT = 192;

unsigned NewVolumeWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}

bool NewVolumeOverflowed (void)
{
    return s_Page.Layout.bOverflow;
}

TRect NewVolumeDialog (void)
{
    return s_Page.Dialog;
}

bool NewVolumeReady (const TNewVolume *pNew)
{
    // A size to give it, and a name to call it. libhfs refuses an empty volume
    // name outright (hfs.c:58), so the button that would produce one is the
    // wrong place to find that out.
    return pNew->nSizes != 0 && pNew->Name[0] != '\0';
}

// "612 MB free on the card" — or, when nothing fits, the reason there is no
// choice to make. The screen says which of the two rather than greying a
// control and leaving the reason to be guessed at.
static void NoteText (const TNewVolume *pNew)
{
    if (pNew->nSizes == 0)
    {
        StrAppend (s_Note, sizeof s_Note, 0, Str (StrCardFull));
        return;
    }
    unsigned n = StrAppendNumber (s_Note, sizeof s_Note, 0, pNew->nFreeMB);
    n = StrAppend (s_Note, sizeof s_Note, n, " ");
    n = StrAppend (s_Note, sizeof s_Note, n, Str (StrUnitMb));
    n = StrAppend (s_Note, sizeof s_Note, n, " ");
    StrAppend (s_Note, sizeof s_Note, n, Str (StrFreeOnCard));
}

void NewVolumeSync (TNewVolume *pNew)
{
    if (pNew->nSizes > NEWVOLUME_SIZES)
    {
        pNew->nSizes = NEWVOLUME_SIZES;
    }
    if (pNew->nPick >= pNew->nSizes)
    {
        pNew->nPick = pNew->nSizes == 0 ? 0 : pNew->nSizes - 1;
    }

    for (unsigned i = 0; i < pNew->nSizes; i++)
    {
        unsigned n = StrAppendNumber (s_SizeText[i], sizeof s_SizeText[i], 0,
                                      pNew->SizeMB[i]);
        n = StrAppend (s_SizeText[i], sizeof s_SizeText[i], n, " ");
        StrAppend (s_SizeText[i], sizeof s_SizeText[i], n, Str (StrUnitMb));
        s_SizeItems[i].pText = s_SizeText[i];
    }
    NoteText (pNew);

    if (s_nSize >= 0)
    {
        TWidget *pPopup = &s_Widgets[s_nSize];
        pPopup->nItems  = pNew->nSizes;
        pPopup->nChoice = (int) pNew->nPick;
        // Nothing fits: the popup has nothing to offer and says so by going
        // grey, and the note beside the buttons says why.
        WidgetSetState (pPopup, pNew->nSizes == 0 ? StateDisabled : StateNormal);
    }
    if (s_nCreate >= 0)
    {
        WidgetSetState (&s_Widgets[s_nCreate],
                        NewVolumeReady (pNew) ? StateDefault
                                              : (StateDefault | StateDisabled));
    }
}

void NewVolumeDraw (TSurface *pSurface, TNewVolume *pNew)
{
    PageBeginAlert (&s_Page, pSurface, s_Widgets, MAX_WIDGETS, SHEET_WIDTH,
                    SHEET_HEIGHT, Str (StrNewVolumeTitle));
    const TTheme *pTheme = &s_Page.Theme;
    TLayout &Layout = s_Page.Layout;

    PageFooter (&s_Page);
    s_nCreate = PageLast (&s_Page, Str (StrCreate), StateDefault);
    s_nCancel = PageLast (&s_Page, Str (StrCancel), StateNormal);
    // Beside the buttons rather than above them: it is a remark about what
    // pressing Create can and cannot do, which is what the footer note is for.
    PageNote (&s_Page, s_Note, StateDisabled);

    // The wider of the two labels sets both, measured per language rather than
    // chosen: which of "Volume name" and "Size" runs longer is not the same
    // question in English and in French.
    const unsigned nName = GfxTextWidth (pTheme->pBodyFont, Str (StrVolumeName));
    const unsigned nSize = GfxTextWidth (pTheme->pBodyFont, Str (StrSize));
    const unsigned nLabel = nName > nSize ? nName : nSize;

    {
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nFieldHeight,
                                            StateFocused));
        TRect L = RowNext (&Band, nLabel, StateNormal);
        L = Rect (L.nX, L.nY + (int) (pTheme->M.nFieldHeight - pTheme->M.nLineHeight) / 2,
                  L.nWidth, pTheme->M.nLineHeight);
        PageAdd (&s_Page, WidgetLabel, L, Str (StrVolumeName), StateNormal);
        s_nName = (int) s_Page.nCount;
        // The focus starts in the name, which is the one thing here that has to
        // be typed — and it is what keeps Create wearing one ring instead of
        // two, the default one and the focus one saying the same thing.
        TWidget *p = PageAdd (&s_Page, WidgetField, RowRest (&Band, StateFocused),
                              pNew->Name, StateFocused);
        p->pEdit     = pNew->Name;
        p->nEditSize = NEWVOLUME_NAME;
    }
    LayoutRowGap (&Layout);

    {
        TRow Band;
        RowBegin (&Band, pTheme, LayoutRow (&Layout, pTheme->M.nButtonHeight,
                                            StateFocused));
        TRect L = RowNext (&Band, nLabel, StateNormal);
        L = Rect (L.nX, L.nY + (int) (pTheme->M.nButtonHeight - pTheme->M.nLineHeight) / 2,
                  L.nWidth, pTheme->M.nLineHeight);
        PageAdd (&s_Page, WidgetLabel, L, Str (StrSize), StateNormal);
        s_nSize = (int) s_Page.nCount;
        TWidget *p = PageAdd (&s_Page, WidgetPopup, RowRest (&Band, StateFocused),
                              0, StateNormal);
        p->pItems = s_SizeItems;
        p->nItems = pNew->nSizes;
    }

    NewVolumeSync (pNew);
    NewVolumeRepaint (pSurface);
}

void NewVolumeRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

TNewVolumeAction NewVolumeOperate (TNewVolume *pNew, int nIndex)
{
    if (nIndex == s_nCancel)
    {
        return NewVolumeCancel;
    }
    if (nIndex == s_nCreate)
    {
        // Tested here as well as greyed: a screen that trusts its own greying
        // is a screen that can be driven past it, and past this one is a card
        // asked for a volume that does not fit.
        if (!NewVolumeReady (pNew))
        {
            NewVolumeSync (pNew);
            return NewVolumeNothing;
        }
        return NewVolumeCreate;
    }
    if (nIndex == s_nSize && pNew->nSizes != 0)
    {
        const int nPick = s_Widgets[s_nSize].nChoice;
        pNew->nPick = nPick < 0 ? 0 : (unsigned) nPick;
    }
    NewVolumeSync (pNew);
    return NewVolumeNothing;
}
