/*
 * video_sizes_circle.h — which sizes a Macintosh is offered, whichever one.
 *
 * Both engines offer the same sizes. They used not to: Basilisk's list grew to
 * thirteen, 16:9 included, while SheepShaver kept upstream's six and a single
 * free identifier, so the same display gave the PowerPC Macintosh no 1280x720
 * beside 1920x1080. The list is therefore stated here once, and each engine
 * contributes only what its core calls each size.
 *
 * Pure: no Circle, no macemu, so tests/host checks it.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_VIDEO_SIZES_CIRCLE_H
#define OKAPIA_VIDEO_SIZES_CIRCLE_H

#include <stdint.h>

// One size the display could offer, before the depths multiply it.
struct TVideoScreenSize
{
    unsigned nWidth, nHeight;
    uint32_t nId;               // whatever the engine's core calls it
};

// The fixed list, in the order the Monitors control panel shows it.
static const unsigned VIDEO_SCREEN_STANDARD = 13;
extern const unsigned VideoScreenStandard[VIDEO_SCREEN_STANDARD][2];

// Then the display's own size and the one the preferences ask for, each only
// when not already listed.
static const unsigned VIDEO_SCREEN_EXTRA = 2;
static const unsigned VIDEO_SCREEN_CUSTOM = 6;
static const unsigned VIDEO_SCREEN_LEGACY_MAX = VIDEO_SCREEN_STANDARD + VIDEO_SCREEN_EXTRA;
static const unsigned VIDEO_SCREEN_SIZES_MAX = VIDEO_SCREEN_LEGACY_MAX + VIDEO_SCREEN_CUSTOM;
static const unsigned VIDEO_SCREEN_BUFFER = 16 * 1024 * 1024;

struct TVideoSize
{
    unsigned nWidth, nHeight;
};

struct TVideoScreenConfig
{
    TVideoSize Custom[VIDEO_SCREEN_CUSTOM]; // slot numbers are persisted and never recycled
    TVideoSize Hidden[VIDEO_SCREEN_SIZES_MAX];
};

// Preferences glue, compiled separately for both engines.
void VideoPrefsRead (TVideoScreenConfig *pConfig);
void VideoPrefsWrite (const TVideoScreenConfig &Config);

bool VideoSizeEqual (TVideoSize A, TVideoSize B);
bool VideoCustomSizeValid (TVideoSize Size);
bool VideoScreenFits (TVideoSize Size, TVideoSize Output);
bool VideoModeHidden (const TVideoScreenConfig &Config, TVideoSize Size);
bool VideoModeSetHidden (TVideoScreenConfig *pConfig, TVideoSize Size, bool bHidden);
unsigned VideoScreenBufferBytes (TVideoSize Output);
TVideoSize VideoStartSize (const char *pScreen, TVideoSize Output);

// pStandardIds names the fixed sizes, one each in the same order; pExtraIds
// names the extras in the order they turn out to be needed, so a fixed size
// keeps its identifier whatever monitor is plugged in — Mac OS 7.5 and later
// store the Monitors choice by identifier. A zero width or height is no extra.
// Answers how many entries pOut, VIDEO_SCREEN_SIZES_MAX long, received.
unsigned VideoScreenSizeList (const uint32_t *pStandardIds, const uint32_t *pExtraIds,
                              unsigned nDisplayW, unsigned nDisplayH,
                              unsigned nWantW, unsigned nWantH,
                              TVideoScreenSize *pOut,
                              const TVideoScreenConfig *pConfig = nullptr,
                              uint32_t nFirstCustomId = 0);

#endif
