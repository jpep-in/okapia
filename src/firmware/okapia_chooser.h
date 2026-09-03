/*
 * okapia_chooser.h — the screen that asks which System to start.
 *
 * The first real screen of the firmware, and the reason the rest of it exists:
 * a card can hold several Systems, and choosing between them meant editing a
 * text file on another computer.
 *
 * Nothing here knows what HFS is, or what a preferences file looks like. It is
 * handed a list of volumes and answers what the user asked for; the caller does
 * the reading and the writing. That is what lets the whole screen be driven by
 * synthetic events on a development machine, which is where its behaviour is
 * checked — and it is the same division that already holds between the drawing
 * primitives and the frame buffer.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef _okapia_chooser_h
#define _okapia_chooser_h

#include "okapia_widgets.h"

// Enough for any card worth booting from, and the same ceiling the kernel's own
// inventory uses.
static const unsigned CHOOSER_MAX = 8;

// One volume, as the chooser needs it. The strings are copied in rather than
// pointed at: the caller's inventory is free to go away, and a screen that
// outlives its data is a screen that draws freed memory.
struct TChooserVolume
{
    char Path[64];                      // as the preferences name it, no * prefix
    char Name[28];
    char System[32];                    // "7.1.2"; empty when there is none
    bool bBootable;                     // it has a blessed folder
    bool bClean;                        // it was unmounted properly
    bool bReadOnly;                     // mounted with the * prefix (disk.cpp:161)
    bool bMounted;                      // listed in the preferences at all
    unsigned long nFreeKB;
};

struct TChooser
{
    TChooserVolume Volumes[CHOOSER_MAX];
    unsigned       nCount;
    int            nStartup;            // the volume that boots, or -1
};

enum TChooserAction
{
    ChooserNothing,                     // the model changed; repaint and carry on
    ChooserStart,
    ChooserSettings,
    ChooserForgetPram,
    ChooserShutDown
};

// Lays the screen out for this surface and paints it. Call again after anything
// that changes how much room a row wants; ChooserRepaint is enough otherwise.
void ChooserDraw (TSurface *pSurface, TChooser *pChooser);
void ChooserRepaint (TSurface *pSurface);

// The components, for ScreenInit. They are the chooser's own array and stay put
// until the next ChooserDraw.
unsigned ChooserWidgets (TWidget **ppList);

// Brings the controls in step with whichever volume is selected: what is true
// of one is not true of the next, and a tick box left showing the last one's
// answer is worse than no tick box at all.
void ChooserSync (TChooser *pChooser);

// What the control at nIndex means. The model is brought up to date first, so a
// control that only changes the model answers ChooserNothing and the caller has
// nothing to do but repaint.
TChooserAction ChooserOperate (TChooser *pChooser, int nIndex);

// The `disk` lines this model asks for, in order, and how many. The startup
// volume comes first because Basilisk offers them to the ROM in the order
// disk.cpp reads them, and a volume to be mounted read-only carries the `*`
// that disk.cpp:161 looks for. A volume that is not mounted is simply absent.
//
// It is a separate answer rather than something written from inside the screen:
// what the user chose and what a preferences file says are two different
// things, and only one of them can be checked on a development machine.
static const unsigned CHOOSER_LINE = 66;
unsigned ChooserDiskLines (const TChooser *pChooser, char Lines[][CHOOSER_LINE],
                           unsigned nMax);

// Which volume is selected, or -1. The caller needs it to say what it is doing
// in the log, and the tests to say what they checked.
int ChooserSelected (void);

#endif
