/*
 * okapia_display.h — output information and Macintosh screen settings.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _OKAPIA_DISPLAY_H
#define _OKAPIA_DISPLAY_H

#include "../circle/video_sizes_circle.h"
#include "okapia_widgets.h"

struct TDisplaySettings
{
    TVideoScreenConfig Modes;
    TVideoSize Output, Preferred, Start, Selected;
    char Monitor[24];
    bool bConfigAvailable, bHdmiDirectives;
    int nFrameSkip;
    bool bModesChanged, bStartChanged, bRefreshChanged;
};

void DisplayDraw (TSurface *pSurface, TDisplaySettings *pDisplay);
void DisplayRepaint (TSurface *pSurface);
unsigned DisplayWidgets (TWidget **ppList);
// True when the command row asks to open the modal editor.
bool DisplayOperate (TDisplaySettings *pDisplay, int nIndex, unsigned nCell);

enum TNewResolutionAction
{
    NewResolutionNothing,
    NewResolutionCreate,
    NewResolutionCancel
};

void NewResolutionDraw (TSurface *pSurface, const TDisplaySettings *pDisplay);
void NewResolutionRepaint (TSurface *pSurface);
unsigned NewResolutionWidgets (TWidget **ppList);
struct TScreen;
void NewResolutionSync (const TDisplaySettings *pDisplay, TScreen *pScreen = nullptr);
TNewResolutionAction NewResolutionOperate (TDisplaySettings *pDisplay, int nIndex);

#endif
