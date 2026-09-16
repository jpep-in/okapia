/*
 * video_sizes_circle.cpp — see video_sizes_circle.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video_sizes_circle.h"

const unsigned VideoScreenStandard[VIDEO_SCREEN_STANDARD][2] =
{
    {  512,  384 },
    {  640,  480 },
    {  800,  600 },
    { 1024,  768 },
    { 1152,  870 },
    { 1280,  720 },
    { 1280,  960 },
    { 1280, 1024 },
    { 1600,  900 },
    { 1600, 1200 },
    { 1920, 1080 },
    { 1920, 1200 },
    { 2560, 1440 },
};

static bool Listed (const TVideoScreenSize *pList, unsigned nCount,
                    unsigned nWidth, unsigned nHeight)
{
    for (unsigned i = 0; i < nCount; i++)
    {
        if (pList[i].nWidth == nWidth && pList[i].nHeight == nHeight)
        {
            return true;
        }
    }
    return false;
}

unsigned VideoScreenSizeList (const u32 *pStandardIds, const u32 *pExtraIds,
                              unsigned nDisplayW, unsigned nDisplayH,
                              unsigned nWantW, unsigned nWantH,
                              TVideoScreenSize *pOut)
{
    unsigned nCount = 0;
    for (unsigned i = 0; i < VIDEO_SCREEN_STANDARD; i++)
    {
        pOut[nCount].nWidth  = VideoScreenStandard[i][0];
        pOut[nCount].nHeight = VideoScreenStandard[i][1];
        pOut[nCount].nId     = pStandardIds[i];
        nCount++;
    }

    const unsigned Extra[VIDEO_SCREEN_EXTRA][2] =
    {
        { nDisplayW, nDisplayH },
        { nWantW,    nWantH    },
    };
    unsigned nExtras = 0;
    for (unsigned i = 0; i < VIDEO_SCREEN_EXTRA; i++)
    {
        if (   Extra[i][0] == 0 || Extra[i][1] == 0
            || Listed (pOut, nCount, Extra[i][0], Extra[i][1]))
        {
            continue;
        }
        pOut[nCount].nWidth  = Extra[i][0];
        pOut[nCount].nHeight = Extra[i][1];
        pOut[nCount].nId     = pExtraIds[nExtras++];
        nCount++;
    }
    return nCount;
}
