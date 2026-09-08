/*
 * compositor_circle.cpp — see compositor_circle.h.
 *
 * Lifted out of the Basilisk video driver unchanged in behaviour: the 16x16
 * grid, the shadow copy, the three scaling paths. Only the plumbing moved.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "compositor_circle.h"

#include <circle/timer.h>
#include <circle/util.h>

bool CompositorPlan (TCompositor *pC)
{
    if (   pC->nWidth == 0 || pC->nHeight == 0
        || pC->nWidth > pC->nOutputWidth || pC->nHeight > pC->nOutputHeight)
    {
        return false;
    }
    if ((u64) pC->nBytesPerRow * pC->nHeight > pC->nShadowBytes)
    {
        return false;
    }

    pC->nScale = 1;
    while (   pC->nWidth  * (pC->nScale + 1) <= pC->nOutputWidth
           && pC->nHeight * (pC->nScale + 1) <= pC->nOutputHeight)
    {
        pC->nScale++;
    }
    pC->nOriginX = (pC->nOutputWidth  - pC->nWidth  * pC->nScale) / 2;
    pC->nOriginY = (pC->nOutputHeight - pC->nHeight * pC->nScale) / 2;

    // Whatever the last mode left outside the new image would otherwise stay on
    // screen forever: nothing ever writes those pixels again.
    memset (pC->pOutput, 0, (size_t) pC->nOutputPitch * pC->nOutputHeight);
    pC->bFullRedraw = true;
    return true;
}

void CompositorRun (TCompositor *pC)
{
    const unsigned nStart     = CTimer::GetClockTicks ();
    const unsigned nOutBytes  = pC->nOutputBits / 8;
    // Pixels that share a byte cannot be split across tiles: at 1 bpp a byte
    // holds eight of them. Align the tile edges to whole source bytes.
    const unsigned nAlign     = (pC->nSourceBits < 8) ? (8 / pC->nSourceBits) : 1;

    static u8 RowBuffer[4096 * 4];
    if ((u64) pC->nWidth * nOutBytes > sizeof RowBuffer)
    {
        return;
    }

    // A Finder sitting still changes almost nothing between two frames, and
    // writing the output is the expensive half — 12x more so once QEMU has a
    // display attached and tracks dirty pages. So compare first: the guest
    // buffer is ordinary memory and reading it is cheap. Upstream does the same
    // on X11 (update_display_dynamic, video_x.cpp:2343) with the same 16x16
    // grid, and the shadow copy is what makes the comparison possible.
    const unsigned nBoxes = 16;
    unsigned nDirty = 0;

    /*
     *  What is compared, and what is left until later
     *
     *  A Finder at rest costs the whole comparison and produces nothing: 256
     *  tiles, 300 KB, and 0 boxes drawn — measured at 1 225 us a frame, seven
     *  per cent of wall time for no pixels. Upstream's answer is to spread the
     *  scan over eight ticks (update_display_dynamic, video_x.cpp:2343), so
     *  that each frame looks at an eighth of the screen.
     *
     *  Spreading alone is wrong, and it is wrong in a way a benchmark does not
     *  show and an eye does at once: a change covering many tiles is then
     *  *noticed* a few tiles at a time and therefore *drawn* a few tiles at a
     *  time, over eight frames. A menu comes down as a mosaic, a window opens
     *  in scattered blocks — a wipe effect nobody asked for. The number
     *  improved and the screen got worse.
     *
     *  So the rule here is not "look at an eighth". It is: **the screen is
     *  never drawn from a partial scan.** A frame first looks at the cheap
     *  set — wherever the screen was moving last time, plus one eighth of the
     *  rest by rotation — and if that finds nothing, nothing is drawn and
     *  nothing can tear. The moment it finds anything at all, the remaining
     *  tiles are compared too, before a single pixel goes out, so whatever
     *  changed appears whole and in one frame.
     *
     *  What that buys is the still screen, which is most of the time: 1 225 us
     *  becomes about 200. What it costs is that a frame in which anything
     *  moves pays the full comparison, exactly as it always did. The moving
     *  set with its one-tile margin is still worth keeping: it is what makes
     *  the pointer's own tile part of the cheap set, so a moving cursor is
     *  found in the first pass instead of waiting for its turn.
     */
    const unsigned SCAN_PHASES = 8;
    u16 Dirty[16];
    u16 Moving[16];
    memset (Dirty, 0, sizeof Dirty);
    memset (Moving, 0, sizeof Moving);

    bool bAnything = pC->bFullRedraw;

    for (unsigned nPass = 0; nPass < 2; nPass++)
    {
        // The second pass only happens because the first found something, and
        // it looks at everything the first left alone.
        if (nPass == 1 && !bAnything)
        {
            break;
        }
        if (nPass == 1 && pC->bFullRedraw)
        {
            break;                      // the first pass already looked at all
        }
        if (nPass == 1)
        {
            pC->nFullScans++;
        }

        for (unsigned by = 0; by < nBoxes; by++)
        {
            const unsigned y0 = by * pC->nHeight / nBoxes;
            const unsigned y1 = (by + 1) * pC->nHeight / nBoxes;
            if (y1 == y0)
            {
                continue;
            }

            for (unsigned bx = 0; bx < nBoxes; bx++)
            {
                if ((Dirty[by] & (1 << bx)) != 0)
                {
                    continue;           // already known
                }

                const bool bCheap = pC->bFullRedraw
                                 || (pC->Announced[by] & (1 << bx)) != 0
                                 || (pC->Watched[by] & (1 << bx)) != 0
                                 || ((by * nBoxes + bx) % SCAN_PHASES) == pC->nScanPhase;
                if (nPass == 0 ? !bCheap : bCheap)
                {
                    continue;           // not this pass's business
                }

                const unsigned x0 = (bx * pC->nWidth / nBoxes) & ~(nAlign - 1);
                const unsigned x1 = (bx == nBoxes - 1)
                                  ? pC->nWidth
                                  : (((bx + 1) * pC->nWidth / nBoxes) & ~(nAlign - 1));
                if (x1 <= x0)
                {
                    continue;
                }
                const unsigned nSpan = (x1 - x0) * pC->nSourceBits / 8;

                bool bDirty = pC->bFullRedraw || (pC->Announced[by] & (1 << bx)) != 0;
                for (unsigned y = y0; !bDirty && y < y1; y++)
                {
                    const u32 nOff = y * pC->nBytesPerRow + x0 * pC->nSourceBits / 8;
                    if (memcmp (pC->pSource + nOff, pC->pShadow + nOff, nSpan) != 0)
                    {
                        bDirty = true;
                    }
                }
                if (bDirty)
                {
                    Dirty[by] |= (u16) (1 << bx);
                    nDirty++;
                    bAnything = true;
                }
            }
        }
    }

    /*
     *  Drawing, one run of adjacent tiles at a time
     *
     *  A row of sixteen adjacent dirty tiles is one span of pixels: converting
     *  640 of them once beats converting 40 of them sixteen times.
     */
    for (unsigned by = 0; by < nBoxes; by++)
    {
        const unsigned y0 = by * pC->nHeight / nBoxes;
        const unsigned y1 = (by + 1) * pC->nHeight / nBoxes;
        const u16 nDirtyMask = Dirty[by];
        if (y1 == y0 || nDirtyMask == 0)
        {
            continue;
        }

        // A one-tile margin around what moved, in both directions, so motion
        // crossing a tile edge is already in the cheap set when it arrives.
        const u16 nHalo = (u16) (nDirtyMask | (nDirtyMask << 1) | (nDirtyMask >> 1));
        Moving[by] |= nHalo;
        if (by > 0)          Moving[by - 1] |= nHalo;
        if (by + 1 < nBoxes) Moving[by + 1] |= nHalo;

        for (unsigned bx = 0; bx < nBoxes; bx++)
        {
            if ((nDirtyMask & (1 << bx)) == 0)
            {
                continue;
            }
            // The run of adjacent dirty tiles starting here.
            unsigned bxEnd = bx;
            while (bxEnd + 1 < nBoxes && (nDirtyMask & (1 << (bxEnd + 1))) != 0)
            {
                bxEnd++;
            }

            const unsigned x0 = (bx * pC->nWidth / nBoxes) & ~(nAlign - 1);
            const unsigned x1 = (bxEnd == nBoxes - 1)
                              ? pC->nWidth
                              : (((bxEnd + 1) * pC->nWidth / nBoxes) & ~(nAlign - 1));
            const unsigned nWidth = (x1 > x0) ? (x1 - x0) : 0;
            const unsigned nSpan  = nWidth * pC->nSourceBits / 8;
            bx = bxEnd;
            if (nWidth == 0)
            {
                continue;
            }

            for (unsigned y = y0; y < y1; y++)
            {
                const u32 nOff = y * pC->nBytesPerRow + x0 * pC->nSourceBits / 8;
                const u8 *pSrc = pC->pSource + nOff;

                memcpy (pC->pShadow + nOff, pSrc, nSpan);
                (*pC->pConvertRow) (RowBuffer, pSrc, nSpan);

                u8 *pDst = pC->pOutput
                         + (pC->nOriginY + y * pC->nScale) * pC->nOutputPitch
                         + (pC->nOriginX + x0 * pC->nScale) * nOutBytes;

                if (pC->nScale == 1)
                {
                    memcpy (pDst, RowBuffer, nWidth * nOutBytes);
                }
                else if (pC->nScale == 2 && ((uintptr) pDst & 7) == 0)
                {
                    // Two output pixels are one 64-bit store, which halves them.
                    u64 *pOut = (u64 *) pDst;
                    const u32 *pIn = (const u32 *) RowBuffer;
                    for (unsigned x = 0; x < nWidth; x++)
                    {
                        u64 v = pIn[x];
                        pOut[x] = v | (v << 32);
                    }
                }
                else
                {
                    u32 *pOut = (u32 *) pDst;
                    const u32 *pIn = (const u32 *) RowBuffer;
                    for (unsigned x = 0; x < nWidth; x++)
                    {
                        for (unsigned t = 0; t < pC->nScale; t++)
                        {
                            *pOut++ = pIn[x];
                        }
                    }
                }

                for (unsigned t = 1; t < pC->nScale; t++)
                {
                    memcpy (pDst + t * pC->nOutputPitch, pDst,
                            nWidth * pC->nScale * nOutBytes);
                }
            }
        }
    }

    pC->bFullRedraw = false;
    pC->nScanPhase = (pC->nScanPhase + 1) % SCAN_PHASES;

    /*
     *  How long a tile stays in the cheap set after it stops moving
     *
     *  Overwriting the watched set with this frame's movement gives it one
     *  frame of memory, and that is too short for the one thing on screen that
     *  is always moving slowly: the pointer. Advancing a pixel every second or
     *  third frame — which is what it does at the lower end of the control
     *  panel's range, and on any scaled mode — it leaves the set empty between
     *  its own steps, so the next step is not in the cheap set and has to wait
     *  its turn in the rotation. Up to an eighth of a second, and at random.
     *  Felt, and reported, as a pointer that catches for an instant and then
     *  goes on; found nowhere in the numbers, because the frames it costs are
     *  frames in which the compositor correctly decided nothing had changed.
     *
     *  A countdown per tile costs 256 bytes and one pass of decrements.
     */
    static const u8 WATCH_FRAMES = 12;
    for (unsigned by = 0; by < nBoxes; by++)
    {
        u16 nWatch = 0;
        for (unsigned bx = 0; bx < nBoxes; bx++)
        {
            if ((Moving[by] & (1 << bx)) != 0)
            {
                pC->WatchFor[by][bx] = WATCH_FRAMES;
            }
            else if (pC->WatchFor[by][bx] != 0)
            {
                pC->WatchFor[by][bx]--;
            }
            if (pC->WatchFor[by][bx] != 0)
            {
                nWatch |= (u16) (1 << bx);
            }
        }
        pC->Watched[by] = nWatch;
    }
    memset (pC->Announced, 0, sizeof pC->Announced);
    pC->nDirtyBoxes += nDirty;
    pC->nUsec += (unsigned) (CTimer::GetClockTicks () - nStart);
    pC->nFrames++;
}

void CompositorAnnounce (TCompositor *pC, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0 || pC->nWidth == 0 || pC->nHeight == 0)
    {
        return;
    }

    // Clamped rather than trusted: the announcement comes from the guest, and a
    // rectangle running off the screen would set bits for tiles that do not
    // exist. The tile boundaries are the ones CompositorRun computes, so the
    // arithmetic has to be the same arithmetic.
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w, y1 = y + h;
    if (x1 > (int) pC->nWidth)  x1 = (int) pC->nWidth;
    if (y1 > (int) pC->nHeight) y1 = (int) pC->nHeight;
    if (x1 <= x0 || y1 <= y0)
    {
        return;
    }

    const unsigned nBoxes = 16;
    for (unsigned by = 0; by < nBoxes; by++)
    {
        const int ty0 = (int) (by * pC->nHeight / nBoxes);
        const int ty1 = (int) ((by + 1) * pC->nHeight / nBoxes);
        if (y1 <= ty0 || y0 >= ty1)
        {
            continue;
        }
        for (unsigned bx = 0; bx < nBoxes; bx++)
        {
            const int tx0 = (int) (bx * pC->nWidth / nBoxes);
            const int tx1 = (int) ((bx + 1) * pC->nWidth / nBoxes);
            if (x1 > tx0 && x0 < tx1)
            {
                pC->Announced[by] |= (u16) (1 << bx);
            }
        }
    }
}

void CompositorConvert16To32 (u8 *pDest, const u8 *pSource, u32 nSourceBytes)
{
    u32 *q = (u32 *) pDest;
    for (u32 i = 0; i < nSourceBytes; i += 2)
    {
        unsigned v = ((unsigned) pSource[i] << 8) | pSource[i + 1];
        unsigned r = (v >> 10) & 0x1F;
        unsigned g = (v >> 5)  & 0x1F;
        unsigned b =  v        & 0x1F;
        *q++ = 0xFF000000
             |  (r << 3 | r >> 2)
             | ((g << 3 | g >> 2) << 8)
             | ((b << 3 | b >> 2) << 16);
    }
}

void CompositorConvert32To32 (u8 *pDest, const u8 *pSource, u32 nSourceBytes)
{
    u32 *q = (u32 *) pDest;
    for (u32 i = 0; i < nSourceBytes; i += 4)
    {
        *q++ = 0xFF000000
             |  (u32) pSource[i + 1]
             | ((u32) pSource[i + 2] << 8)
             | ((u32) pSource[i + 3] << 16);
    }
}
