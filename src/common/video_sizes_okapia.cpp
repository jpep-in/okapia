/*
 * video_sizes_okapia.cpp — see video_sizes_okapia.h.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "video_sizes_okapia.h"
#include <stdio.h>

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

unsigned VideoScreenSizeList (const uint32_t *pStandardIds, const uint32_t *pExtraIds,
                              unsigned nDisplayW, unsigned nDisplayH,
                              unsigned nWantW, unsigned nWantH,
                              TVideoScreenSize *pOut,
                              const TVideoScreenConfig *pConfig, uint32_t nFirstCustomId)
{
    unsigned nCount = 0;
    for (unsigned i = 0; i < VIDEO_SCREEN_STANDARD; i++)
    {
        pOut[nCount].nWidth  = VideoScreenStandard[i][0];
        pOut[nCount].nHeight = VideoScreenStandard[i][1];
        pOut[nCount].nId     = pStandardIds[i];
        nCount++;
    }

    const TVideoSize Half = VideoHalfSize (nDisplayW, nDisplayH);
    const TVideoSize Third = VideoTwoThirdsSize (nDisplayW, nDisplayH);
    const unsigned Extra[VIDEO_SCREEN_EXTRA][2] =
    {
        { nDisplayW,   nDisplayH   },
        { nWantW,      nWantH      },
        { Half.nWidth, Half.nHeight },
        { Third.nWidth, Third.nHeight },
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
    if (pConfig != nullptr)
    {
        unsigned nLegacy = nCount;
        for (unsigned i = 0; i < VIDEO_SCREEN_CUSTOM; i++)
        {
            const TVideoSize Size = pConfig->Custom[i];
            if (!VideoCustomSizeValid (Size))
                continue;
            unsigned nAt = 0;
            while (nAt < nCount && !VideoSizeEqual (Size, {pOut[nAt].nWidth, pOut[nAt].nHeight}))
                nAt++;
            if (nAt < VIDEO_SCREEN_STANDARD)
                continue;
            if (nAt < nCount)
            {
                if (nAt >= nLegacy)
                    continue;
                for (unsigned j = nAt + 1; j < nCount; j++)
                    pOut[j - 1] = pOut[j];
                nCount--;
                nLegacy--;
            }
            pOut[nCount++] = {Size.nWidth, Size.nHeight, nFirstCustomId + i};
        }
    }
    return nCount;
}

TVideoSize VideoHalfSize (unsigned nDisplayW, unsigned nDisplayH)
{
    const TVideoSize Half = {nDisplayW / 2 / 8 * 8, nDisplayH / 2};
    return VideoCustomSizeValid (Half) ? Half : TVideoSize {0, 0};
}

TVideoSize VideoTwoThirdsSize (unsigned nDisplayW, unsigned nDisplayH)
{
    const TVideoSize Size = {nDisplayW * 2 / 3 / 8 * 8, nDisplayH * 2 / 3};
    return VideoCustomSizeValid (Size) ? Size : TVideoSize {0, 0};
}

bool VideoSizeEqual (TVideoSize A, TVideoSize B)
{
    return A.nWidth == B.nWidth && A.nHeight == B.nHeight;
}

bool VideoCustomSizeValid (TVideoSize Size)
{
    // Whole bytes at every packed depth, and within Circle's output limits.
    return Size.nWidth >= 320 && Size.nWidth <= 4096 && Size.nWidth % 8 == 0
        && Size.nHeight >= 200 && Size.nHeight <= 2160;
}

bool VideoScreenFits (TVideoSize Size, TVideoSize Output)
{
    return Size.nWidth != 0 && Size.nHeight != 0
        && Size.nWidth <= Output.nWidth && Size.nHeight <= Output.nHeight
        && (uint64_t) Size.nWidth * Size.nHeight <= VIDEO_SCREEN_BUFFER;
}

bool VideoModeHidden (const TVideoScreenConfig &Config, TVideoSize Size)
{
    for (const TVideoSize &Hidden : Config.Hidden)
        if (VideoSizeEqual (Hidden, Size))
            return true;
    return false;
}

bool VideoModeSetHidden (TVideoScreenConfig *pConfig, TVideoSize Size, bool bHidden)
{
    if (Size.nWidth == 0 || Size.nHeight == 0 || VideoSizeEqual (Size, {640, 480}))
        return false;

    TVideoSize *pFree = nullptr;
    for (TVideoSize &Hidden : pConfig->Hidden)
    {
        if (VideoSizeEqual (Hidden, Size))
        {
            if (!bHidden)
                Hidden = {};
            return true;
        }
        if (pFree == nullptr && Hidden.nWidth == 0)
            pFree = &Hidden;
    }
    if (!bHidden)
        return true;
    if (pFree == nullptr)
        return false;
    *pFree = Size;
    return true;
}

unsigned VideoScreenBufferBytes (TVideoSize Output)
{
    const uint64_t nBytes = (uint64_t) Output.nWidth * Output.nHeight * 4;
    return nBytes > VIDEO_SCREEN_BUFFER ? VIDEO_SCREEN_BUFFER : (unsigned) nBytes;
}

TVideoSize VideoStartSize (const char *pScreen, TVideoSize Output)
{
    int w, h;
    if (pScreen != nullptr && sscanf (pScreen, "%*[^/]/%d/%d", &w, &h) == 2 && w >= 0 && h >= 0)
        return {w == 0 ? Output.nWidth : (unsigned) w, h == 0 ? Output.nHeight : (unsigned) h};
    return {640, 480};
}
