/*
 * video_shared_circle.h — the Macintosh's screen, minus which Macintosh.
 *
 * The two engines present the display to their cores in genuinely different
 * ways: Basilisk hands the platform a monitor_desc and asks it to describe
 * modes, while SheepShaver runs the Mac's own native video driver and asks for
 * a VModes table and a frame buffer at a Mac address. Everything *between*
 * those contracts and the compositor is the same question asked twice — which
 * modes this output can show, which converter a depth needs, how the palette
 * becomes a lookup table, how often a frame is worth compositing.
 *
 * It was written twice, and the two copies had already drifted: only one of
 * them repeated the palette across all 256 entries, and the engine that did not
 * drew four and sixteen colours as coloured noise while 256 was perfect. That
 * is the class of bug this file exists to make impossible — a rule stated once
 * cannot be half-fixed.
 *
 * What stays in each engine's video_circle.cpp is what actually differs: its
 * core's contract, its mode identifiers, and where its guest frame buffer comes
 * from — the heap on one side, a fixed area inside the Mac's own address space
 * on the other.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_VIDEO_SHARED_CIRCLE_H
#define OKAPIA_VIDEO_SHARED_CIRCLE_H

#include <circle/types.h>

#include "video_sizes_circle.h"

// Every depth a Macintosh of this era offers, and the Monitors control panel
// names them all: black and white, 4, 16 and 256 colours, thousands, millions.
// Greyscale is not a depth — the Mac sends a grey palette at the same depth.
extern const unsigned VideoScreenDepths[6];
static const unsigned VIDEO_SCREEN_DEPTHS = 6;


// Upstream's own arithmetic, which rounds up rather than down: at one bit a
// 641-pixel row is 81 bytes and not 80. Both trees carry it as
// TrivialBytesPerRow(), keyed on their own depth enumeration; this is the same
// sum keyed on the number of bits, so neither enumeration reaches this file.
unsigned VideoScreenRowBytes (unsigned nWidth, unsigned nBits);

/*
 *  Opening and closing
 */

// Borrow the output the firmware holds. The geometry is only known afterwards,
// and the mode list depends on it, which is why this is separate from the
// shadow below: an engine has to ask what fits before it knows how big a frame
// it will ever hand over.
bool VideoScreenOpen (void);

// Take the shadow buffer, at the largest guest frame this engine will produce.
// Idempotent, and deliberately so: the Macintosh goes round more than once and
// nothing here is ever given back, so a second start finds it already there
// (docs/topics/startup-and-shutdown.md).
bool VideoScreenShadow (u32 nBytes);

// Forget the mode, keep the output and the shadow. There is nothing to release.
void VideoScreenClose (void);

unsigned VideoScreenOutputWidth (void);
unsigned VideoScreenOutputHeight (void);
u32      VideoScreenShadowBytes (void);

/*
 *  Which modes this output can show
 */

// The sizes this display is offered: the standard list, then the display's own
// size when it is at least 640x480, then the one the "screen" preference asks
// for, in upstream's own spelling — win/640/480, dga/1280/720 — so a prefs file
// brought over from a desktop Basilisk II means the same here; a 0 there is the
// display's own width or height. The identifiers are the engine's
// (video_sizes_circle.h). *pWantW and *pWantH receive the size to start in,
// 640x480 when the preference says nothing.
unsigned VideoScreenSizes (const u32 *pStandardIds, const u32 *pExtraIds,
                           TVideoScreenSize *pOut,
                           unsigned *pWantW, unsigned *pWantH);

// Called once per surviving mode, in the order the sizes and depths are given.
typedef void TVideoScreenSink (void *pContext, const TVideoScreenSize *pSize,
                               unsigned nBits, unsigned nBytesPerRow);

// Every size that fits the output unscaled, at every depth that fits the frame
// buffer, and a log line naming the sizes that survived. The filtering is the shared part; what each engine builds out of the
// results is not, which is why this hands them over rather than returning a
// table of its own.
void VideoScreenEnumerate (const TVideoScreenSize *pSizes, unsigned nSizes,
                           u32 nMaxBufferBytes,
                           TVideoScreenSink *pSink, void *pContext);

/*
 *  Putting a mode into effect
 */

// Choose the row converter for this depth, point the compositor at the guest's
// buffer, work out the scale and the origin, and say what it settled on.
// False when the mode does not fit — the caller must not draw after that.
bool VideoScreenApply (const u8 *pSource, unsigned nWidth, unsigned nHeight,
                       unsigned nBytesPerRow, unsigned nBits);

// 256 entries of red-green-blue, the Mac's palette *repeated* when the mode has
// fewer — which is not tidiness: Blit_Expand_4_To_32 reads ExpandMap[c >> 4]
// and then ExpandMap[c] with the low nibble unmasked, so entry i and entry
// i & (n-1) have to hold the same colour. Pass the entries the mode actually
// has; this repeats them.
void VideoScreenPalette (const u8 *pRGB, unsigned nEntries);

// What the screen should show no longer follows from the guest's bytes.
void VideoScreenInvalidate (void);

// The Macintosh saying what it changed. Only SheepShaver has anything to say.
void VideoScreenAnnounce (int x, int y, int w, int h);

/*
 *  One vertical blank
 */

// Composite if this VBL is one of the ones we keep, then report every five
// seconds and, when the rate is dynamic, re-measure it. True if it composited.
bool VideoScreenVBL (void);

// Read the frameskip preference. 0 is Dynamic, and it is the default.
void VideoScreenReadPrefs (void);

#endif
