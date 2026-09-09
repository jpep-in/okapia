/*
 * okapia_widgets.cpp — components: geometry, state, hit testing. Never pixels.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_widgets.h"

void WidgetClear (TWidget *pWidget)
{
    pWidget->Type      = WidgetLabel;
    pWidget->Rect      = Rect (0, 0, 0, 0);
    pWidget->pText     = 0;
    pWidget->nState    = StateNormal;
    pWidget->nValue    = 0;
    pWidget->nSpan     = 0;
    pWidget->pIcon     = 0;
    pWidget->Paint     = 0;
    pWidget->nGroup    = 0;
    pWidget->pItems    = 0;
    pWidget->nItems    = 0;
    pWidget->nTop      = 0;
    pWidget->nChoice   = -1;
    pWidget->pEdit     = 0;
    pWidget->nEditSize = 0;
    pWidget->nCaret    = 0;
    pWidget->nAnchor   = 0;
    pWidget->pColumns  = 0;
    pWidget->nColumns  = 0;
    pWidget->nCell     = 0;
}

/*
 *  Lists
 */

// The rows live inside the frame's rule, which is one pixel on every side.
static TRect ListInner (const TWidget *pWidget)
{
    return RectInset (pWidget->Rect, 1, 1);
}

// A menu's rows are tighter than a list's: a list row is tall enough for a
// folder icon, and a menu carries words.
static unsigned RowHeight (const TWidget *pWidget, const TTheme *pTheme)
{
    return pWidget->Type == WidgetMenu ? pTheme->M.nMenuRow : pTheme->M.nRowHeight;
}

// A column is as wide as its heading or its mark, whichever is the broader, with
// a gutter on each side. Measured rather than chosen, because these headings are
// translated and French runs longer than English almost everywhere.
static unsigned ColumnWidth (const TTheme *pTheme, const TListColumn *pColumn)
{
    const unsigned nText = pColumn->pHeader != 0
                         ? GfxTextWidth (pTheme->pBodyFont, pColumn->pHeader) : 0;
    const unsigned nWide = nText > pTheme->M.nCheckSize ? nText : pTheme->M.nCheckSize;
    return nWide + 2 * pTheme->M.nGap;
}

static unsigned ColumnsWidth (const TWidget *pWidget, const TTheme *pTheme)
{
    unsigned n = 0;
    for (unsigned i = 0; i < pWidget->nColumns; i++)
    {
        n += ColumnWidth (pTheme, &pWidget->pColumns[i]);
    }
    return n;
}

// The heading band, and a rule's worth of air under it.
static unsigned HeaderHeight (const TWidget *pWidget, const TTheme *pTheme)
{
    return pWidget->nColumns == 0 ? 0 : pTheme->M.nLineHeight + pTheme->M.nStroke;
}

// The band of column headings inside the frame, empty when there are none. The
// rows start under it, which is why every other measurement goes through here.
static TRect WidgetListHeader (const TWidget *pWidget, const TTheme *pTheme)
{
    const unsigned nHigh = HeaderHeight (pWidget, pTheme);
    if (nHigh == 0)
    {
        return Rect (0, 0, 0, 0);
    }
    const TRect Inner = ListInner (pWidget);
    return Rect (Inner.nX, Inner.nY, Inner.nWidth, nHigh);
}

unsigned WidgetListVisible (const TWidget *pWidget, const TTheme *pTheme)
{
    const unsigned nRow = RowHeight (pWidget, pTheme);
    if (nRow == 0)
    {
        return 0;
    }
    const unsigned nHigh = ListInner (pWidget).nHeight - HeaderHeight (pWidget, pTheme);
    return nHigh / nRow;
}

TRect WidgetListScroller (const TWidget *pWidget, const TTheme *pTheme)
{
    const unsigned nVisible = WidgetListVisible (pWidget, pTheme);
    if (pWidget->nItems <= nVisible)
    {
        return Rect (0, 0, 0, 0);       // nothing to scroll: no scroller at all
    }
    const TRect Inner = ListInner (pWidget);
    const unsigned nBar = pTheme->M.nScrollbarWidth;
    // Beside the rows and not beside the headings: a scroller that reached up
    // into the heading band would be scrolling the column names too.
    const unsigned nHead = HeaderHeight (pWidget, pTheme);
    return Rect (Inner.nX + (int) Inner.nWidth - (int) nBar, Inner.nY + (int) nHead,
                 nBar, Inner.nHeight - nHead);
}

// What the rows may use, once the heading band and the scroller have taken
// theirs.
static TRect ListRows (const TWidget *pWidget, const TTheme *pTheme)
{
    TRect Inner = ListInner (pWidget);
    const unsigned nHead = HeaderHeight (pWidget, pTheme);
    Inner = Rect (Inner.nX, Inner.nY + (int) nHead, Inner.nWidth, Inner.nHeight - nHead);
    const TRect Bar = WidgetListScroller (pWidget, pTheme);
    if (Bar.nWidth != 0)
    {
        Inner.nWidth -= Bar.nWidth;
    }
    return Inner;
}

// The left edge of column nColumn (1-based), inside the row band.
static int ColumnLeft (const TWidget *pWidget, const TTheme *pTheme, unsigned nColumn)
{
    const TRect Rows = ListRows (pWidget, pTheme);
    int nX = Rows.nX + (int) Rows.nWidth - (int) ColumnsWidth (pWidget, pTheme);
    for (unsigned i = 0; i + 1 < nColumn; i++)
    {
        nX += (int) ColumnWidth (pTheme, &pWidget->pColumns[i]);
    }
    return nX;
}

unsigned WidgetListColumnAt (const TWidget *pWidget, const TTheme *pTheme, int nX)
{
    for (unsigned i = pWidget->nColumns; i >= 1; i--)
    {
        if (nX >= ColumnLeft (pWidget, pTheme, i))
        {
            return i;
        }
    }
    return 0;
}

// Where one cell is drawn. nItem is an index into the items, not a row on the
// screen.
static TRect WidgetListCell (const TWidget *pWidget, const TTheme *pTheme, int nItem,
                             unsigned nColumn)
{
    if (nColumn == 0 || nColumn > pWidget->nColumns || nItem < 0)
    {
        return Rect (0, 0, 0, 0);
    }
    const TRect Rows = ListRows (pWidget, pTheme);
    const unsigned nRow = RowHeight (pWidget, pTheme);
    const int nY = Rows.nY + (int) ((unsigned) nItem - pWidget->nTop) * (int) nRow;
    const unsigned nWide = ColumnWidth (pTheme, &pWidget->pColumns[nColumn - 1]);
    const unsigned nSize = pTheme->M.nCheckSize;
    return Rect (ColumnLeft (pWidget, pTheme, nColumn) + (int) (nWide - nSize) / 2,
                 nY + ((int) nRow - (int) nSize) / 2, nSize, nSize);
}

int WidgetListItemAt (const TWidget *pWidget, const TTheme *pTheme, int nX, int nY)
{
    const TRect Rows = ListRows (pWidget, pTheme);
    if (!RectContains (Rows, nX, nY))
    {
        return -1;
    }
    const unsigned nRow = RowHeight (pWidget, pTheme);
    const unsigned nIndex = pWidget->nTop + (unsigned) (nY - Rows.nY) / nRow;
    return nIndex < pWidget->nItems ? (int) nIndex : -1;
}

void WidgetListReveal (TWidget *pWidget, const TTheme *pTheme)
{
    const unsigned nVisible = WidgetListVisible (pWidget, pTheme);
    if (nVisible == 0 || pWidget->nChoice < 0)
    {
        return;
    }
    const unsigned nWanted = (unsigned) pWidget->nChoice;
    if (nWanted < pWidget->nTop)
    {
        pWidget->nTop = nWanted;
    }
    else if (nWanted >= pWidget->nTop + nVisible)
    {
        pWidget->nTop = nWanted - nVisible + 1;
    }
    // And never past the end: a list scrolled to show blank rows below its last
    // item reads as a list that has lost something.
    if (pWidget->nItems > nVisible && pWidget->nTop > pWidget->nItems - nVisible)
    {
        pWidget->nTop = pWidget->nItems - nVisible;
    }
}

static void DrawList (TSurface *pSurface, const TTheme *pTheme, const TWidget *pWidget)
{
    pTheme->DrawListFrame (pSurface, pWidget->Rect, pTheme);

    // The headings, once, above the rows and outside what scrolls.
    const TRect Head = WidgetListHeader (pWidget, pTheme);
    if (Head.nHeight != 0)
    {
        for (unsigned i = 0; i < pWidget->nColumns; i++)
        {
            const unsigned nWide = ColumnWidth (pTheme, &pWidget->pColumns[i]);
            const TRect Box = Rect (ColumnLeft (pWidget, pTheme, i + 1), Head.nY, nWide,
                                    pTheme->M.nLineHeight);
            GfxTextBox (pSurface, pTheme->pBodyFont, Box, pWidget->pColumns[i].pHeader,
                        ColorBlack, TextAlignCenter);
        }
        GfxFill (pSurface,
                 Rect (Head.nX, Head.nY + (int) Head.nHeight - (int) pTheme->M.nStroke,
                       Head.nWidth, pTheme->M.nStroke), ColorBlack);
    }

    const TRect    Rows     = ListRows (pWidget, pTheme);
    const unsigned nRow     = RowHeight (pWidget, pTheme);
    const unsigned nVisible = WidgetListVisible (pWidget, pTheme);

    for (unsigned i = 0; i < nVisible; i++)
    {
        const unsigned nIndex = pWidget->nTop + i;
        if (nIndex >= pWidget->nItems)
        {
            break;
        }
        const TListItem *pItem = &pWidget->pItems[nIndex];
        const bool bChosen = (int) nIndex == pWidget->nChoice;
        const TRect Row = Rect (Rows.nX, Rows.nY + (int) (i * nRow), Rows.nWidth, nRow);

        // Background from the theme, contents here, then the theme again to pick
        // the row out. Drawing the contents in ordinary ink and inverting
        // afterwards is what lets one list serve the systems, the settings and
        // the catalogue of phase 17 without being written three times.
        pTheme->DrawListRow (pSurface, Row, pItem->nState, pTheme);

        int nInk = Row.nX + (int) pTheme->M.nGap;
        if (pItem->pIcon != 0)
        {
            const int nIconY = Row.nY + ((int) Row.nHeight
                                         - (int) (pItem->pIcon->nHeight * pTheme->nIconScale)) / 2;
            GfxImage (pSurface, pItem->pIcon, nInk, nIconY, ColorBlack, pTheme->nIconScale);
            nInk += (int) (pItem->pIcon->nWidth * pTheme->nIconScale) + (int) pTheme->M.nGap;
        }
        // A row the screen has singled out — the volume that boots, the choice
        // a menu already holds — wears a bullet at its right. A mark rather
        // than a weight, because the ladder has no third face and a row is not
        // a place to invent one.
        int nRight = Row.nX + (int) Row.nWidth - (int) pTheme->M.nGap;

        // The marks first, so the name knows where it has to stop. Each is
        // drawn on its own with no label beside it: the heading above says what
        // it means, which is the whole point of a column.
        // An action row answers none of the columns, so it draws none of their
        // marks — a greyed tick box there would say it has an answer it is not
        // allowed to give, which is not what is true of it.
        if (pWidget->nColumns != 0 && !pItem->bAction)
        {
            nRight = ColumnLeft (pWidget, pTheme, 1) - (int) pTheme->M.nGap;
            for (unsigned c = 1; c <= pWidget->nColumns; c++)
            {
                const TRect Cell = WidgetListCell (pWidget, pTheme, (int) nIndex, c);
                unsigned nCellState = pItem->nCell[c - 1];
                if ((pWidget->nState & StateFocused) && pWidget->nCell == c && bChosen)
                {
                    nCellState |= StateFocused;
                }
                if (pWidget->pColumns[c - 1].bRadio)
                {
                    pTheme->DrawRadio (pSurface, Cell, "", nCellState, pTheme);
                }
                else
                {
                    pTheme->DrawCheckbox (pSurface, Cell, "", nCellState, pTheme);
                }
            }
        }

        if ((pItem->nState & StateChecked) && pWidget->nColumns == 0)
        {
            const unsigned nDot = pTheme->M.nCheckSize / 3 < 3 ? 3
                                                               : pTheme->M.nCheckSize / 3;
            GfxCircleFill (pSurface,
                           Rect (nRight - (int) nDot,
                                 Row.nY + ((int) Row.nHeight - (int) nDot) / 2, nDot, nDot),
                           ColorBlack);
            nRight -= (int) nDot + (int) pTheme->M.nGap;
        }
        if (pItem->pText != 0)
        {
            const TRect Text = Rect (nInk, Row.nY, (unsigned) (nRight - nInk), Row.nHeight);
            GfxTextBox (pSurface, pTheme->pBodyFont, Text, pItem->pText,
                        (pItem->nState & StateDisabled) ? ColorDim : ColorBlack,
                        TextAlignLeft);
        }
        if (bChosen)
        {
            pTheme->DrawSelection (pSurface, Row, pTheme);
        }
    }

    const TRect Bar = WidgetListScroller (pWidget, pTheme);
    if (Bar.nWidth != 0)
    {
        pTheme->DrawScrollbar (pSurface, Bar, pWidget->nTop, nVisible, pWidget->nItems,
                               pTheme);
    }
    if (pWidget->nState & StateFocused)
    {
        pTheme->DrawFocusRing (pSurface, pWidget->Rect, 0, pTheme->M.nFocusGap, pTheme);
    }
}

void WidgetDraw (TSurface *pSurface, const TTheme *pTheme, const TWidget *pWidget)
{
    switch (pWidget->Type)
    {
    case WidgetLabel:
        pTheme->DrawLabel (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetParagraph:
        pTheme->DrawParagraph (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState,
                               pTheme);
        break;

    case WidgetTitle:
        pTheme->DrawTitle (pSurface, pWidget->Rect, pWidget->pText, pTheme);
        break;

    case WidgetAlert:
        pTheme->DrawAlert (pSurface, pWidget->Rect, pTheme);
        break;

    case WidgetButton:
        pTheme->DrawButton (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetIconButton:
        pTheme->DrawIconButton (pSurface, pWidget->Rect, pWidget->Paint, pWidget->nState,
                                pTheme);
        break;

    case WidgetIcon:
        pTheme->DrawIcon (pSurface, pWidget->Rect, pWidget->Paint, pWidget->nState, pTheme);
        break;

    case WidgetCheckbox:
        pTheme->DrawCheckbox (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetRadio:
        pTheme->DrawRadio (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState, pTheme);
        break;

    case WidgetList:
    case WidgetMenu:
        DrawList (pSurface, pTheme, pWidget);
        break;

    case WidgetScrollbar:
        // On its own it is the same three numbers: where the view starts, how
        // much it shows, how much there is.
        pTheme->DrawScrollbar (pSurface, pWidget->Rect, pWidget->nValue, pWidget->nSpan,
                               pWidget->nItems, pTheme);
        break;

    case WidgetPopup:
        // Its label is whichever item is chosen, when it has any: a pop-up that
        // shows a fixed string is a button wearing a triangle.
        pTheme->DrawPopup (pSurface, pWidget->Rect,
                           pWidget->pItems != 0 && pWidget->nChoice >= 0
                               ? pWidget->pItems[pWidget->nChoice].pText
                               : pWidget->pText,
                           pWidget->nState, pTheme);
        break;

    case WidgetField:
        pTheme->DrawField (pSurface, pWidget->Rect, pWidget->pText, pWidget->nState,
                           WidgetFieldMark (pWidget), pTheme);
        break;

    case WidgetProgress:
        pTheme->DrawProgress (pSurface, pWidget->Rect, pWidget->nValue, pTheme);
        break;

    case WidgetSeparator:
        pTheme->DrawSeparator (pSurface, pWidget->Rect, pTheme);
        break;
    }
}

void WidgetDrawAll (TSurface *pSurface, const TTheme *pTheme, const TWidget *pList,
                    unsigned nCount)
{
    for (unsigned i = 0; i < nCount; i++)
    {
        WidgetDraw (pSurface, pTheme, &pList[i]);
    }
}

int WidgetHit (const TWidget *pList, unsigned nCount, int nX, int nY)
{
    // Backwards, so the last drawn is the first hit: the array order is the
    // stacking order, and there is no other one.
    for (int i = (int) nCount - 1; i >= 0; i--)
    {
        const TWidget *p = &pList[i];
        if (p->nState & StateDisabled)
        {
            continue;
        }
        switch (p->Type)
        {
        case WidgetLabel:
        case WidgetParagraph:
        case WidgetTitle:
        case WidgetAlert:
        case WidgetIcon:
        case WidgetProgress:
        case WidgetSeparator:
            continue;                   // decoration takes no clicks
        default:
            break;
        }
        if (RectContains (p->Rect, nX, nY))
        {
            return i;
        }
    }
    return -1;
}

void WidgetSetState (TWidget *pWidget, unsigned nState)
{
    pWidget->nState = nState | (pWidget->nState & (StateFocused | StateCaret));
}

bool WidgetFocusable (const TWidget *pWidget)
{
    if (pWidget->nState & (StateDisabled | StateNoFocus))
    {
        return false;
    }
    switch (pWidget->Type)
    {
    case WidgetButton:
    case WidgetIconButton:
    case WidgetCheckbox:
    case WidgetRadio:
    case WidgetList:
    case WidgetPopup:
    case WidgetField:
        return true;
    default:
        return false;
    }
}

unsigned WidgetButtonWidth (const TTheme *pTheme, const char *pText, unsigned nState)
{
    const unsigned nText = GfxTextWidth (
        (nState & StateDefault) ? pTheme->pBodyBoldFont : pTheme->pBodyFont,
        pText);
    const unsigned nWidth = nText + 2 * pTheme->M.nButtonPadX;
    return nWidth < pTheme->M.nButtonMinWidth ? pTheme->M.nButtonMinWidth : nWidth;
}

/*
 *  Editable fields
 *
 *  The bytes are UTF-8 and the caret is a byte offset, so moving it means
 *  stepping over a whole character and not a byte: a caret parked inside a
 *  two-byte "é" would split it on the next insertion, and what came out would
 *  not be text any more.
 */

static unsigned Length (const char *p)
{
    unsigned n = 0;
    while (p[n] != '\0')
    {
        n++;
    }
    return n;
}

static bool Continuation (char c)
{
    return ((unsigned char) c & 0xC0) == 0x80;
}

void WidgetFieldClick (TWidget *pWidget, const TTheme *pTheme, int nX, bool bExtend)
{
    if (pWidget->pEdit == 0)
    {
        return;
    }
    pWidget->nCaret = GfxTextOffsetAt (pTheme->pBodyFont, pWidget->pEdit,
                                       nX - pWidget->Rect.nX - ThemeFieldInset (pTheme));
    if (!bExtend)
    {
        pWidget->nAnchor = pWidget->nCaret;
    }
}

TFieldMark WidgetFieldMark (const TWidget *pWidget)
{
    TFieldMark m = { pWidget->nCaret, pWidget->nAnchor };
    return m;
}

// One scrap for the whole firmware: there is one field on a screen at a time
// and nothing here allocates. Twenty-eight bytes, the same as a field, because
// what it carries between two of them is a volume name.
static char s_Scrap[28];

bool WidgetFieldHasSelection (const TWidget *pWidget)
{
    return pWidget->pEdit != 0 && pWidget->nAnchor != pWidget->nCaret;
}

static unsigned SelFrom (const TWidget *p)
{
    return p->nAnchor < p->nCaret ? p->nAnchor : p->nCaret;
}

static unsigned SelTo (const TWidget *p)
{
    return p->nAnchor < p->nCaret ? p->nCaret : p->nAnchor;
}

// Removes the nBytes bytes starting at nAt.
static void Remove (TWidget *pWidget, unsigned nAt, unsigned nBytes)
{
    const unsigned nLen = Length (pWidget->pEdit);
    for (unsigned i = nAt; i + nBytes <= nLen; i++)
    {
        pWidget->pEdit[i] = pWidget->pEdit[i + nBytes];
    }
}

static void DeleteSelection (TWidget *pWidget)
{
    if (!WidgetFieldHasSelection (pWidget))
    {
        return;
    }
    const unsigned nFrom = SelFrom (pWidget), nTo = SelTo (pWidget);
    Remove (pWidget, nFrom, nTo - nFrom);
    pWidget->nCaret = pWidget->nAnchor = nFrom;
}

void WidgetFieldSelectAll (TWidget *pWidget)
{
    if (pWidget->pEdit != 0)
    {
        pWidget->nAnchor = 0;
        pWidget->nCaret  = Length (pWidget->pEdit);
    }
}

void WidgetFieldCopy (const TWidget *pWidget)
{
    if (!WidgetFieldHasSelection (pWidget))
    {
        return;                         // copying nothing must not empty the scrap
    }
    const unsigned nFrom = SelFrom (pWidget), nTo = SelTo (pWidget);
    unsigned n = nTo - nFrom;
    if (n > sizeof s_Scrap - 1)
    {
        n = sizeof s_Scrap - 1;
        // Never in the middle of a character: a scrap cut through a two-byte
        // letter is not text any more, and it would be pasted as one.
        while (n > 0 && Continuation (pWidget->pEdit[nFrom + n]))
        {
            n--;
        }
    }
    for (unsigned i = 0; i < n; i++)
    {
        s_Scrap[i] = pWidget->pEdit[nFrom + i];
    }
    s_Scrap[n] = '\0';
}

void WidgetFieldCut (TWidget *pWidget)
{
    WidgetFieldCopy (pWidget);
    DeleteSelection (pWidget);
}

void WidgetFieldInsert (TWidget *pWidget, unsigned nCode)
{
    if (pWidget->pEdit == 0 || nCode < 0x20 || nCode == 0x7F)
    {
        return;                         // control codes are not text
    }
    // Typing over a selection replaces it, which is what every field has done
    // since anyone could select anything.
    DeleteSelection (pWidget);

    // UTF-8, because that is what the faces are indexed by and what every
    // string in this firmware already is.
    char Bytes[2];
    unsigned nBytes;
    if (nCode < 0x80)
    {
        Bytes[0] = (char) nCode;
        nBytes = 1;
    }
    else if (nCode < 0x800)
    {
        Bytes[0] = (char) (0xC0 | (nCode >> 6));
        Bytes[1] = (char) (0x80 | (nCode & 0x3F));
        nBytes = 2;
    }
    else
    {
        return;                         // nothing a keyboard produces reaches here
    }

    const unsigned nLen = Length (pWidget->pEdit);
    if (nLen + nBytes + 1 > pWidget->nEditSize)
    {
        return;                         // full: refuse rather than truncate silently
    }
    for (unsigned i = nLen + 1; i-- > pWidget->nCaret; )
    {
        pWidget->pEdit[i + nBytes] = pWidget->pEdit[i];
    }
    for (unsigned i = 0; i < nBytes; i++)
    {
        pWidget->pEdit[pWidget->nCaret + i] = Bytes[i];
    }
    pWidget->nCaret += nBytes;
    pWidget->nAnchor = pWidget->nCaret;
}

void WidgetFieldPaste (TWidget *pWidget)
{
    if (pWidget->pEdit == 0)
    {
        return;
    }
    DeleteSelection (pWidget);
    // Through the same door as a keystroke, so the room left, the UTF-8 and the
    // caret are all handled in one place.
    const unsigned char *p = (const unsigned char *) s_Scrap;
    while (*p != '\0')
    {
        unsigned nCode = *p;
        if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
        {
            nCode = (unsigned) ((*p & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        }
        else
        {
            p++;
        }
        WidgetFieldInsert (pWidget, nCode);
    }
}

void WidgetFieldBackspace (TWidget *pWidget)
{
    if (pWidget->pEdit == 0)
    {
        return;
    }
    if (WidgetFieldHasSelection (pWidget))
    {
        DeleteSelection (pWidget);
        return;
    }
    if (pWidget->nCaret == 0)
    {
        return;
    }
    unsigned nAt = pWidget->nCaret - 1;
    while (nAt > 0 && Continuation (pWidget->pEdit[nAt]))
    {
        nAt--;
    }
    Remove (pWidget, nAt, pWidget->nCaret - nAt);
    pWidget->nCaret = pWidget->nAnchor = nAt;
}

void WidgetFieldDelete (TWidget *pWidget)
{
    if (pWidget->pEdit == 0)
    {
        return;
    }
    if (WidgetFieldHasSelection (pWidget))
    {
        DeleteSelection (pWidget);
        return;
    }
    if (pWidget->pEdit[pWidget->nCaret] == '\0')
    {
        return;
    }
    unsigned nEnd = pWidget->nCaret + 1;
    while (pWidget->pEdit[nEnd] != '\0' && Continuation (pWidget->pEdit[nEnd]))
    {
        nEnd++;
    }
    Remove (pWidget, pWidget->nCaret, nEnd - pWidget->nCaret);
    pWidget->nAnchor = pWidget->nCaret;
}

void WidgetFieldCaret (TWidget *pWidget, int nStep, bool bExtend)
{
    if (pWidget->pEdit == 0)
    {
        return;
    }
    // An arrow with a selection standing collapses it to the end it points at,
    // and moves no further: that is what the mark is for.
    if (!bExtend && WidgetFieldHasSelection (pWidget))
    {
        pWidget->nCaret = nStep < 0 ? SelFrom (pWidget) : SelTo (pWidget);
        pWidget->nAnchor = pWidget->nCaret;
        return;
    }
    if (nStep < 0)
    {
        if (pWidget->nCaret > 0)
        {
            pWidget->nCaret--;
            while (pWidget->nCaret > 0 && Continuation (pWidget->pEdit[pWidget->nCaret]))
            {
                pWidget->nCaret--;
            }
        }
    }
    else if (pWidget->pEdit[pWidget->nCaret] != '\0')
    {
        pWidget->nCaret++;
        while (pWidget->pEdit[pWidget->nCaret] != '\0'
               && Continuation (pWidget->pEdit[pWidget->nCaret]))
        {
            pWidget->nCaret++;
        }
    }
    if (!bExtend)
    {
        pWidget->nAnchor = pWidget->nCaret;
    }
}

void WidgetFieldHome (TWidget *pWidget, bool bExtend)
{
    pWidget->nCaret = 0;
    if (!bExtend)
    {
        pWidget->nAnchor = 0;
    }
}

void WidgetFieldEnd (TWidget *pWidget, bool bExtend)
{
    if (pWidget->pEdit != 0)
    {
        pWidget->nCaret = Length (pWidget->pEdit);
        if (!bExtend)
        {
            pWidget->nAnchor = pWidget->nCaret;
        }
    }
}
