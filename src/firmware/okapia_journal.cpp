/*
 * okapia_journal.cpp — the Journal page.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_journal.h"
#include "okapia_page.h"
#include "okapia_strings.h"

#include <string.h>

static TPage     s_Page;
static TWidget   s_Widgets[32];
static TListItem s_Items[JOURNAL_LINES + 1];
static TWidget  *s_pList;
static int       s_nPrevious = -1, s_nSerial = -1, s_nCounters = -1;
static unsigned  s_nLines;

void JournalClear (TJournal *pJournal)
{
    pJournal->nText   = 0;
    pJournal->nEscape = 0;
    pJournal->bCut    = false;
    pJournal->Text[0] = '\0';
}

void JournalAppend (TJournal *pJournal, const char *pData, unsigned nBytes)
{
    for (unsigned i = 0; i < nBytes; i++)
    {
        const char c = pData[i];

        // A colour sequence is ESC, '[', parameters and a final letter. The
        // state lives in the page and not in this loop because the log is read
        // in chunks, and one may end between the two.
        if (pJournal->nEscape == 1)
        {
            pJournal->nEscape = c == '[' ? 2 : 0;
            continue;
        }
        if (pJournal->nEscape == 2)
        {
            if (c >= '@' && c <= '~')
                pJournal->nEscape = 0;
            continue;
        }
        if (c == 0x1B)
        {
            pJournal->nEscape = 1;
            continue;
        }
        if (c == '\r' || c == '\0')
            continue;

        if (pJournal->nText == JOURNAL_TEXT)
        {
            // The oldest quarter goes, so that a long log costs a move once in
            // a while and not at every byte.
            const unsigned nDrop = JOURNAL_TEXT / 4;
            memmove (pJournal->Text, pJournal->Text + nDrop, JOURNAL_TEXT - nDrop);
            pJournal->nText -= nDrop;
            pJournal->bCut = true;
        }
        pJournal->Text[pJournal->nText++] = (c < 0x20 && c != '\n') ? ' ' : c;
    }
    pJournal->Text[pJournal->nText] = '\0';
}

// Cuts the text into lines in place, as many as the list holds, the newest.
// The log is padded with blanks to the end of its last sector, and both that and
// a cut beginning are dealt with here: a second call finds nothing left to do.
static unsigned Split (TJournal *pJournal)
{
    unsigned nEnd = pJournal->nText;
    while (nEnd != 0 && (pJournal->Text[nEnd - 1] == ' ' || pJournal->Text[nEnd - 1] == '\n'
                         || pJournal->Text[nEnd - 1] == '\0'))
        nEnd--;
    pJournal->nText = nEnd;
    pJournal->Text[nEnd] = '\0';

    // A window cut out of the middle of the log begins in the middle of a
    // line: that fragment goes, once.
    unsigned nStart = 0;
    if (pJournal->bCut)
    {
        while (nStart < nEnd && pJournal->Text[nStart] != '\n' && pJournal->Text[nStart] != '\0')
            nStart++;
        if (nStart < nEnd)
            nStart++;
        memmove (pJournal->Text, pJournal->Text + nStart, nEnd - nStart);
        nEnd -= nStart;
        nStart = 0;
        pJournal->nText = nEnd;
        pJournal->Text[nEnd] = '\0';
        pJournal->bCut = false;
    }

    unsigned nLines = 0;
    for (unsigned i = nStart; i < nEnd; i++)
        if (pJournal->Text[i] == '\n' || pJournal->Text[i] == '\0')
            nLines++;
    nLines++;                           // the last line has no terminator of its own
    unsigned nSkip = nLines > JOURNAL_LINES ? nLines - JOURNAL_LINES : 0;

    unsigned n = 0;
    unsigned i = nStart;
    while (i <= nEnd && n < JOURNAL_LINES)
    {
        const unsigned nBegin = i;
        while (i < nEnd && pJournal->Text[i] != '\n' && pJournal->Text[i] != '\0')
            i++;
        pJournal->Text[i] = '\0';
        if (nSkip != 0)
            nSkip--;
        else
        {
            TListItem &Item = s_Items[n++];
            Item = {};
            Item.pText = pJournal->Text + nBegin;
        }
        i++;
    }
    return n;
}

static void Sync (const TJournal *pJournal)
{
    WidgetSetState (&s_Widgets[s_nPrevious], pJournal->bPrevious ? StateChecked : StateNormal);
    WidgetSetState (&s_Widgets[s_nSerial], pJournal->bSerial ? StateChecked : StateNormal);
    WidgetSetState (&s_Widgets[s_nCounters], pJournal->bCounters ? StateChecked : StateNormal);
}

// A tick box and its words, from the left of the row, centred on the band.
static int Tick (TRow *pRow, const TRect &rBand, TStringId Id)
{
    const TTheme *pTheme = &s_Page.Theme;
    const unsigned nWidth = pTheme->M.nCheckSize + pTheme->M.nGap
                          + GfxTextWidth (pTheme->pBodyFont, Str (Id));
    TRect Box = RowNext (pRow, nWidth, StateNormal);
    Box = Rect (Box.nX, rBand.nY + (int) (rBand.nHeight - pTheme->M.nCheckSize) / 2,
                Box.nWidth, pTheme->M.nCheckSize);
    const int nIndex = (int) s_Page.nCount;
    PageAdd (&s_Page, WidgetCheckbox, Box, Str (Id), StateNormal);
    return nIndex;
}

void JournalDraw (TSurface *pSurface, TJournal *pJournal)
{
    PageBeginTabbed (&s_Page, pSurface, s_Widgets, sizeof s_Widgets / sizeof s_Widgets[0],
                     PageTabJournal);
    TLayout &Layout = s_Page.Layout;
    const TTheme &Theme = s_Page.Theme;
    const unsigned nTickRow = Theme.M.nCheckSize > Theme.M.nLineHeight ? Theme.M.nCheckSize
                                                                      : Theme.M.nLineHeight;

    // The switches first, and the log under them taking what is left: the
    // list is the one thing on the page that wants all the room there is.
    TPageGroup Group;
    PageGroupBegin (&s_Page, &Group, Str (StrJournalGroup));
    {
        const TRect Band = LayoutRow (&Layout, nTickRow, StateFocused);
        TRow Row;
        RowBegin (&Row, &Theme, Band);
        s_nSerial   = Tick (&Row, Band, StrJournalSerial);
        s_nCounters = Tick (&Row, Band, StrJournalCounters);
    }
    LayoutRowGap (&Layout);
    {
        const char *pNote = Str (StrJournalHint);
        const TRect Room = LayoutRow (&Layout, Theme.M.nLineHeight, StateNormal);
        const unsigned nWanted = GfxTextWrapHeight (Theme.pBodyFont, Room.nWidth, pNote,
                                                    Theme.M.nLineHeight);
        TRect Text = Room;
        if (nWanted > Room.nHeight)
        {
            LayoutSkip (&Layout, nWanted - Room.nHeight);
            Text = Rect (Room.nX, Room.nY, Room.nWidth, nWanted);
        }
        PageAdd (&s_Page, WidgetParagraph, Text, pNote, StateDisabled);
    }
    PageGroupEnd (&s_Page, &Group);
    LayoutSectionGap (&Layout);

    // The tick box for the other session goes under the list, and is claimed
    // first from the bottom: the list takes what is left between the two.
    const TRect PreviousBand = LayoutRowBottom (&Layout, nTickRow, StateFocused);
    LayoutRowGapBottom (&Layout);

    const unsigned nReserve = 2 * ThemeReach (&Theme, StateFocused) + Theme.M.nGap / 2
                            + Theme.M.nStroke;
    const unsigned nRoom = LayoutRoom (&Layout);
    s_pList = PageAdd (&s_Page, WidgetList,
                       LayoutRow (&Layout, nRoom > nReserve ? nRoom - nReserve : 0, StateFocused),
                       nullptr, StateNormal);
    {
        TRow Row;
        RowBegin (&Row, &Theme, PreviousBand);
        s_nPrevious = Tick (&Row, PreviousBand, StrJournalPrevious);
    }
    s_pList->nListRowHeight = Theme.M.nMenuRow;
    s_pList->pItems = s_Items;

    s_nLines = Split (pJournal);
    if (pJournal->nText == 0)
    {
        s_Items[0] = {};
        s_Items[0].pText  = Str (StrJournalEmpty);
        s_Items[0].nState = StateDisabled;
        s_nLines = 0;
        s_pList->nItems  = 1;
        s_pList->nChoice = -1;
    }
    else
    {
        // At the end, where the log is newest, as a terminal would be.
        s_pList->nItems  = s_nLines;
        s_pList->nChoice = (int) s_nLines - 1;
        WidgetListReveal (s_pList, &s_Page.Theme);
    }

    Sync (pJournal);
    JournalRepaint (pSurface);
}

TJournalAction JournalOperate (TJournal *pJournal, int nIndex)
{
    TJournalAction Action = JournalNothing;
    if (nIndex == s_nPrevious)
    {
        pJournal->bPrevious = !pJournal->bPrevious;
        Action = JournalReload;
    }
    else if (nIndex == s_nSerial)
    {
        pJournal->bSerial = !pJournal->bSerial;
        pJournal->bSerialChanged = true;
    }
    else if (nIndex == s_nCounters)
    {
        pJournal->bCounters = !pJournal->bCounters;
        pJournal->bCountersChanged = true;
    }
    Sync (pJournal);
    return Action;
}

TRect JournalDialog (void)
{
    return s_Page.Dialog;
}

bool JournalOverflowed (void)
{
    return s_Page.Layout.bOverflow;
}

unsigned JournalLines (void)
{
    return s_nLines;
}

void JournalRepaint (TSurface *pSurface)
{
    PagePaint (pSurface, &s_Page);
}

unsigned JournalWidgets (TWidget **ppList)
{
    *ppList = s_Widgets;
    return s_Page.nCount;
}
