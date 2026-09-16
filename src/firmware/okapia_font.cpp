/*
 * okapia_font.cpp — choosing a rung of the font ladder.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "okapia_font.h"

const TOkapiaFontSet *FontNearest (unsigned nHeight)
{
    const TOkapiaFontSet *pBest = &OkapiaFaces[0];
    unsigned nBestGap = ~0u;

    for (unsigned i = 0; i < OkapiaFaceCount; i++)
    {
        const unsigned h = OkapiaFaces[i].nHeight;
        const unsigned nGap = h > nHeight ? h - nHeight : nHeight - h;
        if (nGap < nBestGap)
        {
            nBestGap = nGap;
            pBest = &OkapiaFaces[i];
        }
    }
    return pBest;
}
