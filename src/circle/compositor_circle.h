/*
 * compositor_circle.h — the Macintosh's frame buffer onto the board's.
 *
 * Shared by both engines, and that is the whole point of it existing: the
 * arithmetic that scales, centres and skips what did not change is about the
 * Macintosh's screen and the board's, and knows nothing about which emulator
 * filled the first. Basilisk's video had it inline; SheepShaver's arrived
 * without it and drew 640x480 into the corner of a 1280x960 display, which is
 * how it became clear the code belonged in one place rather than two.
 *
 * What it does not own: the Mac's pixel format. Each engine hands in a row
 * converter — Basilisk uses upstream's video_blit.cpp, SheepShaver a small
 * palette expander for the one indexed mode it offers — because that is the
 * part that genuinely differs.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_COMPOSITOR_CIRCLE_H
#define OKAPIA_COMPOSITOR_CIRCLE_H

#include <circle/types.h>

// One row of the Macintosh's pixels into one row of the output's, in the
// output's format. nSourceBytes counts *source* bytes, which is what every
// blitter in this project counts — at 1 bpp that is eight pixels a byte.
typedef void TCompositorConvert (u8 *pDest, const u8 *pSource, u32 nSourceBytes);

struct TCompositor
{
    // The Macintosh's screen, as the Macintosh laid it out.
    const u8 *pSource;
    unsigned  nWidth, nHeight, nBytesPerRow, nSourceBits;

    // The board's, as the firmware granted it.
    u8       *pOutput;
    unsigned  nOutputWidth, nOutputHeight, nOutputPitch, nOutputBits;

    TCompositorConvert *pConvertRow;

    // The previous frame, at least nBytesPerRow * nHeight. Without it every
    // frame is a full redraw, which is affordable headless and is not once a
    // display is attached: QEMU tracks dirty pages then, and the same composite
    // costs 12x more (AGENTS.md).
    u8       *pShadow;
    unsigned  nShadowBytes;

    // Worked out by CompositorPlan, read by CompositorRun.
    unsigned  nScale, nOriginX, nOriginY;

    // Set whenever what the screen should show no longer follows from the
    // guest's bytes — a mode switch, a palette change, a gamma ramp. The
    // shadow is then stale everywhere and comparing it would skip everything.
    bool      bFullRedraw;

    // Tiles the guest said it changed since the last frame, one bit per tile.
    // Filled by CompositorAnnounce, cleared by CompositorRun.
    u16       Announced[16];

    // Where the screen has been moving lately, plus a tile of margin all round.
    // These are compared every frame; the rest are compared one eighth at a
    // time. See CompositorRun.
    //
    // "Lately" and not "last frame": each tile carries a countdown of frames,
    // so a tile that has stopped stays watched for a little while afterwards.
    // One frame of memory looked equivalent and was not — see CompositorRun.
    u16       Watched[16];
    u8        WatchFor[16][16];
    unsigned  nScanPhase;

    // Read by whoever reports; never by the compositor itself. nFullScans
    // counts the frames that had to compare the whole screen — which is every
    // frame in which anything changed at all, because a frame never draws from
    // a partial scan. See CompositorRun.
    u64       nUsec;
    unsigned  nFrames, nDirtyBoxes, nFullScans;
};

// Largest integer scale that still fits, then centre what that gives. Integer
// only: a fractional factor means interpolation, and a Macintosh screen
// interpolated is a Macintosh screen that shimmers. False when the Mac's mode
// does not fit the output at all, or the shadow is too small for it.
bool CompositorPlan (TCompositor *pC);

// One frame. Cheap when nothing moved.
void CompositorRun (TCompositor *pC);

// "The guest changed this rectangle." An announced tile is redrawn without
// being compared, which is the whole saving; every other tile is still compared
// as before.
//
// It is a hint and never the whole story, which is why the comparison stays.
// SheepShaver's video driver calls video_set_dirty_area() from its accelerated
// paths only (gfxaccel.cpp) — QuickDraw writing to the frame buffer the
// ordinary way announces nothing. Trusting the announcements alone would leave
// most of the screen stale, and the failure would look like tearing rather than
// like a missing call. Basilisk announces nothing at all and simply never calls
// this.
void CompositorAnnounce (TCompositor *pC, int x, int y, int w, int h);

// The two converters for the Macintosh's *direct* modes, shared because both
// engines need exactly these and upstream's video_blit.cpp does not supply
// them: its direct-mode blitters assume the host reads pixels the way the Mac
// wrote them, and this one does not — the Mac is big-endian and its 16-bit
// pixel is 0RRRRRGG GGGBBBBB. Handing Screen_blit a direct mode gave a screen
// entirely in one colour at 16 bits and vertical stripes at 32.
//
// Indexed modes do go through Screen_blit, which is what ExpandMap is for.
void CompositorConvert16To32 (u8 *pDest, const u8 *pSource, u32 nSourceBytes);
void CompositorConvert32To32 (u8 *pDest, const u8 *pSource, u32 nSourceBytes);

#endif
