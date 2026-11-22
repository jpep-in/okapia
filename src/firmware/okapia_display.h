/*
 * okapia_display.h — output information and Macintosh screen settings.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _OKAPIA_DISPLAY_H
#define _OKAPIA_DISPLAY_H

#include "../common/video_sizes_okapia.h"
#include "okapia_widgets.h"

struct TDisplaySettings
{
    TVideoScreenConfig Modes;
    TVideoSize Output, Preferred, Start, Selected;
    char Monitor[24];
    bool bConfigAvailable, bHdmiDirectives;
    unsigned nDensity;                  // device pixels per output pixel, 0 as 1
    // A window on a desktop rather than a board's output: no HDMI to report,
    // and the output is the window, the preferred size the screen's.
    bool bWindowed;
    int nFrameSkip;
    bool bModesChanged, bStartChanged, bRefreshChanged;

    // A desktop's window size (HostWindowDescribe), false on a board. Modes as
    // THostWindowMode: 0 sized, 1 fill the screen, 2 full screen.
    bool bWindowSizes;
    unsigned nWindowMode;
    TVideoSize Window, Fill, Screen;
    // What was chosen, for the menu to apply (DisplayWindowChosen).
    unsigned nWantedMode;
    TVideoSize Wanted;
};

void DisplayDraw (TSurface *pSurface, TDisplaySettings *pDisplay);
void DisplayRepaint (TSurface *pSurface);
unsigned DisplayWidgets (TWidget **ppList);
enum TDisplayAction
{
    DisplayNothing,
    DisplayNewResolution,               // the modal editor of a Mac resolution
    DisplayWindowChosen,                // nWantedMode, Wanted: to apply
    DisplayWindowOther                  // the modal editor, for the window
};
TDisplayAction DisplayOperate (TDisplaySettings *pDisplay, int nIndex, unsigned nCell);

enum TNewResolutionAction
{
    NewResolutionNothing,
    NewResolutionCreate,
    NewResolutionCancel
};

// bWindow: the same editor, for a desktop's window size in points.
void NewResolutionDraw (TSurface *pSurface, const TDisplaySettings *pDisplay, bool bWindow = false);
void NewResolutionRepaint (TSurface *pSurface);
unsigned NewResolutionWidgets (TWidget **ppList);
struct TScreen;
void NewResolutionSync (const TDisplaySettings *pDisplay, TScreen *pScreen = nullptr);
TNewResolutionAction NewResolutionOperate (TDisplaySettings *pDisplay, int nIndex);

#endif
