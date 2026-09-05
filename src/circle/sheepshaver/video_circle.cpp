/*
 * video_circle.cpp — SheepShaver's screen, on a Raspberry Pi.
 *
 * A different contract from the other engine's. Basilisk hands the platform a
 * monitor_desc and asks it to describe modes; SheepShaver runs the Macintosh's
 * own native video driver and asks the platform for a table of modes, a frame
 * buffer at a Mac address, and a handful of calls the driver makes into it.
 * video.cpp owns the globals — screen_base, cur_mode, VModes, the palette —
 * and this file fills them in.
 *
 * Deliberately the smallest thing that can put pixels on a screen: one mode,
 * 640x480 in 256 colours, no cursor acceleration, no mode changes. Enough to
 * find out whether the Macintosh boots at all, which is what phase 20 is for.
 * The compositor and the dirty-region work belong to phase 21, and this file is
 * where they will land — video_set_dirty_area() is already the hook, and
 * SheepShaver calls it whether or not it accelerates.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <string.h>

#include "okapia_circle.h"
#include "okapia_output.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "video.h"
#include "video_defs.h"
#include "mac_layout.h"

#define FROM "okapia-ppc-video"

// One mode, and the one every Macintosh of this era can be told about.
static const uint16 MAC_WIDTH  = 640;
static const uint16 MAC_HEIGHT = 480;

// From mac_layout: the frame buffer is inside the Mac's own address space,
// because screen_base is a Mac address and QuickDraw writes through it.
extern uint32 MacFrameBufferGuest (void);

static bool             s_bReady;
static uint8           *s_pMacPixels;   // host view of what the Mac draws into
static unsigned         s_nDirty;       // regions the driver announced since the last VBL
static CBcmFrameBuffer *s_pOutput;
static uint8           *s_pOutputPixels;
static unsigned         s_nOutputWidth, s_nOutputHeight, s_nOutputPitch;
static uint32           s_Palette[256]; // the Mac's colours, in the output's form

/*
 *  Bring the screen up
 *
 *  VModes is a table video.cpp owns and the Mac's driver walks; a zero viType
 *  ends it. cur_mode indexes it.
 */

bool VideoInit (void)
{
    const uint32 nGuest = MacFrameBufferGuest ();
    if (nGuest == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No frame buffer in the Mac's memory");
        return false;
    }

    VModes[0].viType       = DIS_SCREEN;
    VModes[0].viXsize      = MAC_WIDTH;
    VModes[0].viYsize      = MAC_HEIGHT;
    VModes[0].viRowBytes   = MAC_WIDTH;          // 8 bits per pixel, no padding
    VModes[0].viAppleMode  = APPLE_8_BIT;
    VModes[0].viAppleID    = APPLE_640x480;
    VModes[1].viType       = DIS_INVALID;        // end of table

    cur_mode     = 0;
    display_type = DIS_SCREEN;
    screen_base  = nGuest;
    s_pMacPixels = Mac2HostAddr (nGuest);

    memset (s_pMacPixels, 0, (size_t) MAC_WIDTH * MAC_HEIGHT);

    // The output is claimed once for the life of the board, like the other
    // engine's — the Macintosh may go round more than once and a claim per
    // start leaks a frame buffer each time (AGENTS.md).
    s_pOutput = FwOutputClaim ();
    if (s_pOutput == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No frame buffer from Circle");
        return false;
    }
    s_nOutputWidth  = s_pOutput->GetWidth ();
    s_nOutputHeight = s_pOutput->GetHeight ();
    s_nOutputPitch  = s_pOutput->GetPitch ();
    s_pOutputPixels = (uint8 *) (uintptr) s_pOutput->GetBuffer ();

    video_activated = true;
    s_bReady = true;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Screen: %ux%u, 256 colours, Mac buffer at 0x%08X",
                            (unsigned) MAC_WIDTH, (unsigned) MAC_HEIGHT,
                            (unsigned) nGuest);
    return true;
}

void VideoExit (void)
{
    // The output is not given back: see FwOutputClaim.
    video_activated = false;
    s_bReady = false;
}

/*
 *  Once per frame, from the tick
 *
 *  Phase 20 draws the whole screen every time. That is the wasteful answer and
 *  it is the right one for now: measuring a compositor before the Macintosh
 *  boots would be measuring nothing.
 */

void VideoVBL (void)
{
    if (!s_bReady)
    {
        return;
    }
    // The whole screen, every frame, through the palette. Wasteful and correct;
    // what makes it affordable is phase 21's business, and measuring it before
    // the Macintosh boots would be measuring nothing.
    const unsigned nRows = MAC_HEIGHT < s_nOutputHeight ? MAC_HEIGHT : s_nOutputHeight;
    const unsigned nCols = MAC_WIDTH  < s_nOutputWidth  ? MAC_WIDTH  : s_nOutputWidth;
    for (unsigned y = 0; y < nRows; y++)
    {
        const uint8 *pSrc = s_pMacPixels + (size_t) y * MAC_WIDTH;
        uint32 *pDst = (uint32 *) (s_pOutputPixels + (size_t) y * s_nOutputPitch);
        for (unsigned x = 0; x < nCols; x++)
        {
            pDst[x] = s_Palette[pSrc[x]];
        }
    }
    s_nDirty = 0;
}

/*
 *  What the Macintosh's driver tells us
 */

void video_set_palette (void)
{
    if (!s_bReady)
    {
        return;
    }
    // mac_pal is what the Mac asked for. The output is 32 bits per pixel, so
    // the palette becomes a lookup table rather than something the hardware
    // holds — the same choice the other engine makes, and for the same reason:
    // a Pi 5 has no indexed mode to offer.
    for (unsigned i = 0; i < 256; i++)
    {
        s_Palette[i] =   ((uint32) mac_pal[i].red   << 16)
                       | ((uint32) mac_pal[i].green << 8)
                       |  (uint32) mac_pal[i].blue;
    }
}

void video_set_gamma (int n_colors)
{
    // No gamma ramp: the output is what the Mac drew. A ramp would have to set
    // s_bFullRedraw in the compositor that phase 21 brings, since it changes
    // what the screen shows without changing a single guest byte.
    (void) n_colors;
}

void video_set_dirty_area (int x, int y, int w, int h)
{
    // Recorded and not yet used. This is the call Basilisk never makes and the
    // reason phase 21's dirty-region work is cheaper on this engine than on the
    // other: the Macintosh says what it changed instead of being scanned.
    (void) x; (void) y; (void) w; (void) h;
    s_nDirty++;
}

int16 video_mode_change (VidLocals *csSave, uint32 ParamPtr)
{
    // One mode. Refusing plainly beats accepting and drawing nothing.
    (void) csSave; (void) ParamPtr;
    CLogger::Get ()->Write (FROM, LogWarning, "Mode change refused: one mode only");
    return paramErr;
}

bool video_can_change_cursor (void)
{
    // The Mac draws its own pointer into its own frame buffer, which is what
    // arrives on the screen. Claiming a hardware cursor we have not got would
    // leave the Macintosh with no pointer at all.
    return false;
}

void video_set_cursor (void)
{
}

// VideoInstallAccel() is gfxaccel.cpp's, which was the right guess about who
// owns QuickDraw acceleration and the wrong one about who defines the call.

void VideoQuitFullScreen (void)
{
}

// VideoActivated() and VideoSnapshot() are video.cpp's own, not the platform's,
// however video.h reads. Defining them here was a guess from the header, and
// the linker corrected it — which is the point of getting to a link early.
