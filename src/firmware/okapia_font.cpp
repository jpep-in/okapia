/*
 * okapia_font.cpp — the characters the faces carry, and their kerning.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_font.h"

// What the faces are built with, in index order: Latin-1 and Latin Extended-A
// (a French translation, and the names of its neighbours), the dashes, quotes,
// bullet and ellipsis, per mille and guillemets, the euro, the trade mark, the
// arrows, Command and Option. scripts/fetch-fonts.sh keeps the same set in the
// faces; a code a face lacks has an index and no ink.
static const struct { unsigned short nFirst, nLast; } RANGES[] =
{
    { 0x0020, 0x007E }, { 0x00A0, 0x017F }, { 0x2010, 0x2027 }, { 0x2030, 0x2030 },
    { 0x2039, 0x203A }, { 0x20AC, 0x20AC }, { 0x2122, 0x2122 }, { 0x2190, 0x2193 },
    { 0x2318, 0x2318 }, { 0x2325, 0x2325 },
};
static const unsigned RANGE_COUNT = sizeof RANGES / sizeof RANGES[0];

int FontGlyphIndex (int nCode)
{
    int nBase = 0;
    for (unsigned i = 0; i < RANGE_COUNT; i++)
    {
        if (nCode < (int) RANGES[i].nFirst)
        {
            return -1;                  // the ranges ascend: it falls between two
        }
        if (nCode <= (int) RANGES[i].nLast)
        {
            return nBase + nCode - (int) RANGES[i].nFirst;
        }
        nBase += (int) (RANGES[i].nLast - RANGES[i].nFirst + 1);
    }
    return -1;
}

unsigned FontGlyphCount (void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < RANGE_COUNT; i++)
    {
        n += (unsigned) (RANGES[i].nLast - RANGES[i].nFirst + 1);
    }
    return n;
}

int FontKern16 (const TOkapiaFont *pFont, int nLeft, int nRight)
{
    if (pFont->nKern == 0 || nLeft < 0 || nRight < 0)
    {
        return 0;
    }
    const unsigned nKey = (unsigned) nLeft << 16 | (unsigned) nRight;
    unsigned nLow = 0, nHigh = pFont->nKern;
    while (nLow < nHigh)
    {
        const unsigned nMid = (nLow + nHigh) / 2;
        const TOkapiaKern &K = pFont->pKern[nMid];
        const unsigned nAt = (unsigned) K.nLeft << 16 | K.nRight;
        if (nAt == nKey)
        {
            const float f = (float) K.nUnits * pFont->fKern16;
            return (int) (f < 0.0f ? f - 0.5f : f + 0.5f);
        }
        if (nAt < nKey)
            nLow = nMid + 1;
        else
            nHigh = nMid;
    }
    return 0;
}
