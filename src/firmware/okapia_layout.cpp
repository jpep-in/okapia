/*
 * okapia_layout.cpp — where a control goes, without a single coordinate.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_layout.h"

void LayoutBegin (TLayout *pLayout, const TTheme *pTheme, const TRect &rBounds)
{
    pLayout->pTheme    = pTheme;
    pLayout->Free      = rBounds;
    pLayout->bOverflow = false;
}

unsigned LayoutRoom (const TLayout *pLayout)
{
    return pLayout->Free.nHeight;
}

TRect LayoutTop (TLayout *pLayout, unsigned nHeight)
{
    if (nHeight > pLayout->Free.nHeight)
    {
        nHeight = pLayout->Free.nHeight;
        pLayout->bOverflow = true;
    }
    const TRect Band = Rect (pLayout->Free.nX, pLayout->Free.nY,
                             pLayout->Free.nWidth, nHeight);
    pLayout->Free.nY      += (int) nHeight;
    pLayout->Free.nHeight -= nHeight;
    return Band;
}

TRect LayoutBottom (TLayout *pLayout, unsigned nHeight)
{
    if (nHeight > pLayout->Free.nHeight)
    {
        nHeight = pLayout->Free.nHeight;
        pLayout->bOverflow = true;
    }
    pLayout->Free.nHeight -= nHeight;
    return Rect (pLayout->Free.nX, pLayout->Free.nY + (int) pLayout->Free.nHeight,
                 pLayout->Free.nWidth, nHeight);
}

TRect LayoutRow (TLayout *pLayout, unsigned nHeight, unsigned nState)
{
    const unsigned nReach = ThemeReach (pLayout->pTheme, nState);
    const TRect Band = LayoutTop (pLayout, nHeight + 2 * nReach);
    return Rect (Band.nX, Band.nY + (int) nReach, Band.nWidth,
                 Band.nHeight > 2 * nReach ? Band.nHeight - 2 * nReach : 0);
}

void LayoutSkip (TLayout *pLayout, unsigned nAmount)
{
    LayoutTop (pLayout, nAmount);
}

void LayoutRowGap (TLayout *pLayout)
{
    LayoutSkip (pLayout, pLayout->pTheme->M.nRowGap);
}

void LayoutSectionGap (TLayout *pLayout)
{
    LayoutSkip (pLayout, pLayout->pTheme->M.nSectionGap);
}

void RowBegin (TRow *pRow, const TTheme *pTheme, const TRect &rBand)
{
    pRow->pTheme    = pTheme;
    pRow->Free      = rBand;
    pRow->bFirst    = true;
    pRow->bOverflow = false;
}

// A control occupies its own width and, on each side of it, whatever its state
// draws outside itself. Reserving that here rather than at the call site is the
// difference between a row that is right and one that is right until somebody
// forgets: the trailing reach in particular was invisible until a focused
// pop-up at the end of a row put its ring one pixel into the margin.
static void RowClaimLeft (TRow *pRow, unsigned nAmount)
{
    if (nAmount > pRow->Free.nWidth)
    {
        nAmount = pRow->Free.nWidth;
        pRow->bOverflow = true;
    }
    pRow->Free.nX     += (int) nAmount;
    pRow->Free.nWidth -= nAmount;
}

static void RowClaimRight (TRow *pRow, unsigned nAmount)
{
    if (nAmount > pRow->Free.nWidth)
    {
        nAmount = pRow->Free.nWidth;
        pRow->bOverflow = true;
    }
    pRow->Free.nWidth -= nAmount;
}

TRect RowNext (TRow *pRow, unsigned nWidth, unsigned nState)
{
    const unsigned nReach = ThemeReach (pRow->pTheme, nState);
    if (!pRow->bFirst)
    {
        RowClaimLeft (pRow, pRow->pTheme->M.nGap);
    }
    pRow->bFirst = false;
    RowClaimLeft (pRow, nReach);

    if (nWidth > pRow->Free.nWidth)
    {
        nWidth = pRow->Free.nWidth;
        pRow->bOverflow = true;
    }
    const TRect Cell = Rect (pRow->Free.nX, pRow->Free.nY, nWidth, pRow->Free.nHeight);
    RowClaimLeft (pRow, nWidth);
    RowClaimLeft (pRow, nReach);
    return Cell;
}

TRect RowLast (TRow *pRow, unsigned nWidth, unsigned nState)
{
    // Its ring comes off the right edge before the control does. A control
    // placed flush against a margin pushes whatever it wears into it, and that
    // is the failure that only ever showed up on the right and the bottom.
    const unsigned nReach = ThemeReach (pRow->pTheme, nState);
    RowClaimRight (pRow, nReach);

    if (nWidth > pRow->Free.nWidth)
    {
        nWidth = pRow->Free.nWidth;
        pRow->bOverflow = true;
    }
    RowClaimRight (pRow, nWidth);
    const TRect Cell = Rect (pRow->Free.nX + (int) pRow->Free.nWidth, pRow->Free.nY,
                             nWidth, pRow->Free.nHeight);
    RowClaimRight (pRow, nReach);
    return Cell;
}

TRect RowAbut (TRow *pRow, unsigned nWidth)
{
    if (nWidth > pRow->Free.nWidth)
    {
        nWidth = pRow->Free.nWidth;
        pRow->bOverflow = true;
    }
    const TRect Cell = Rect (pRow->Free.nX, pRow->Free.nY, nWidth, pRow->Free.nHeight);
    RowClaimLeft (pRow, nWidth);
    pRow->bFirst = false;
    return Cell;
}

TRect RowRest (TRow *pRow, unsigned nState)
{
    // Everything left, minus what this control wears on either side of itself.
    // Handing over the whole remainder is the obvious version, and it is how a
    // focused pop-up at the end of a row put its ring in the margin.
    const unsigned nReach = ThemeReach (pRow->pTheme, nState);
    if (!pRow->bFirst)
    {
        RowClaimLeft (pRow, pRow->pTheme->M.nGap);
    }
    pRow->bFirst = false;
    RowClaimLeft (pRow, nReach);
    RowClaimRight (pRow, nReach);

    const TRect Rest = pRow->Free;
    pRow->Free.nX    += (int) pRow->Free.nWidth;
    pRow->Free.nWidth = 0;
    return Rest;
}

void RowSkip (TRow *pRow, unsigned nAmount)
{
    RowClaimLeft (pRow, nAmount);
}

TRect RectColumn (const TRect &rRect, unsigned nIndex, unsigned nCount, unsigned nGutter)
{
    if (nCount == 0)
    {
        return rRect;
    }
    const unsigned nTotal = rRect.nWidth;
    const unsigned nGaps  = nGutter * (nCount - 1);
    const unsigned nCell  = nTotal > nGaps ? (nTotal - nGaps) / nCount : 0;
    return Rect (rRect.nX + (int) (nIndex * (nCell + nGutter)), rRect.nY,
                 nCell, rRect.nHeight);
}
