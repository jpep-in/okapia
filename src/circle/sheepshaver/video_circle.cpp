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
 * That contract is all this file is. Which modes fit the output, which
 * converter a depth needs, how the palette becomes a lookup table, how often a
 * frame is worth compositing and what to say about it: all of that is the same
 * question asked of the same compositor, and it lives in
 * video_shared_circle.cpp. It used to live here as well, and the two copies had
 * drifted — this one repeated the palette across 256 entries and, for a while,
 * did not, which drew 4 and 16 colours as coloured noise while 256 was perfect.
 *
 * What is genuinely this engine's: the mode table and its Apple identifiers,
 * the driver's mode change, video_set_dirty_area() — the Macintosh saying what
 * it changed, which the 68k side has no equivalent of — and a frame buffer that
 * lives inside the Mac's own address space, because screen_base is a Mac
 * address and QuickDraw writes through it.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <string.h>

#include "okapia_circle.h"
#include "video_shared_circle.h"

#include "cpu_emulation.h"
#include "main.h"
#include "video.h"
#include "video_defs.h"
#include "mac_layout.h"

#define FROM "okapia-ppc-video"

// mac_layout.cpp: one buffer for every mode, taken at the largest, because a
// mode change must not allocate — the Macintosh changes depth whenever a dialog
// wants more colours.
extern uint32 MacFrameBufferGuest (void);

// The sizes this engine offers. No 512x384: that is a Classic resolution and
// SheepShaver has no Apple identifier for it.
static const TVideoScreenSize SIZES[] =
{
    {  640, 480, APPLE_640x480  },
    {  800, 600, APPLE_800x600  },
    { 1024, 768, APPLE_1024x768 },
};

static bool     s_bReady;
static uint32   s_nGuestBase;       // the Mac address of the frame buffer
static uint32   s_nBufferBytes;     // what the frame area holds
static unsigned s_nModes;

// How many entries the Macintosh actually fills for a mode. Upstream's own
// table (video_x.cpp:259), repeated here because it lives in a platform file
// there and this is that platform file.
static int PaletteSizeOf (uint32 nAppleMode)
{
    switch (nAppleMode)
    {
    case APPLE_1_BIT:  return 2;
    case APPLE_2_BIT:  return 4;
    case APPLE_4_BIT:  return 16;
    case APPLE_8_BIT:  return 256;
    case APPLE_16_BIT: return 32;
    case APPLE_32_BIT: return 256;
    default:           return 0;
    }
}

static unsigned DepthBitsOf (uint32 nAppleMode)
{
    switch (nAppleMode)
    {
    case APPLE_1_BIT:  return 1;
    case APPLE_2_BIT:  return 2;
    case APPLE_4_BIT:  return 4;
    case APPLE_8_BIT:  return 8;
    case APPLE_16_BIT: return 16;
    case APPLE_32_BIT: return 32;
    default:           return 8;
    }
}

/*
 *  Put one mode into effect
 *
 *  Everything a mode change touches on this side is here, so the driver's entry
 *  point below and VideoInit() ask for it the same way and cannot drift apart.
 */

static bool SwitchTo (unsigned nMode)
{
    const VideoInfo &Mode = VModes[nMode];

    cur_mode     = nMode;
    display_type = Mode.viType;
    screen_base  = s_nGuestBase;

    uint8 *pPixels = Mac2HostAddr (s_nGuestBase);
    memset (pPixels, 0, (size_t) Mode.viRowBytes * Mode.viYsize);

    if (!VideoScreenApply (pPixels, Mode.viXsize, Mode.viYsize, Mode.viRowBytes,
                           DepthBitsOf (Mode.viAppleMode)))
    {
        s_bReady = false;
        return false;
    }
    s_bReady = true;
    return true;
}

/*
 *  Bring the screen up
 *
 *  VModes is a table video.cpp owns and the Mac's driver walks; a zero viType
 *  ends it. cur_mode indexes it.
 */

static void AddMode (void *pContext, const TVideoScreenSize *pSize,
                     unsigned nBits, unsigned nBytesPerRow)
{
    (void) pContext;
    VideoInfo &M  = VModes[s_nModes++];
    M.viType      = DIS_SCREEN;
    M.viXsize     = pSize->nWidth;
    M.viYsize     = pSize->nHeight;
    M.viRowBytes  = nBytesPerRow;
    M.viAppleMode = DepthModeForPixelDepth (nBits);
    M.viAppleID   = pSize->nId;
}

bool VideoInit (void)
{
    s_nGuestBase = MacFrameBufferGuest ();
    if (s_nGuestBase == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No frame buffer in the Mac's memory");
        return false;
    }
    s_nBufferBytes = OKAPIA_FRAME_SIZE;

    if (!VideoScreenOpen () || !VideoScreenShadow (s_nBufferBytes))
    {
        return false;
    }
    VideoScreenReadPrefs ();

    s_nModes = 0;
    VideoScreenEnumerate (SIZES, sizeof SIZES / sizeof SIZES[0], s_nBufferBytes,
                          AddMode, 0);
    VModes[s_nModes].viType = DIS_INVALID;   // end of table

    if (s_nModes == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "This output fits no Macintosh mode");
        return false;
    }

    // Open in 640x480 in 256 colours, whatever position that ended up at in the
    // table: it is the mode every Macintosh of this era can be started in, and
    // the driver opens on cur_mode. Opening on VModes[0] instead meant opening
    // in black and white, because the depths are listed smallest first — the
    // Mac corrected itself a few seconds later from its PRAM, but a screen that
    // starts wrong and fixes itself is a screen somebody will report.
    unsigned nDefault = 0;
    for (unsigned i = 0; i < s_nModes; i++)
    {
        if (VModes[i].viXsize == 640 && VModes[i].viYsize == 480
            && VModes[i].viAppleMode == APPLE_8_BIT)
        {
            nDefault = i;
            break;
        }
    }

    if (!SwitchTo (nDefault))
    {
        return false;
    }

    video_activated = true;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "%u modes offered, %u KB frame buffer at 0x%08X",
                            s_nModes, (unsigned) (s_nBufferBytes / 1024),
                            (unsigned) s_nGuestBase);
    return true;
}

void VideoExit (void)
{
    video_activated = false;
    s_bReady = false;
    VideoScreenClose ();
}

/*
 *  Once per frame, from the Macintosh's own VBL
 *
 *  Called through NATIVE_VIDEO_VBL, which the Mac's interrupt path invokes
 *  (emul_op.cpp:321) — so this is the guest's frame, not ours.
 */

void VideoVBL (void)
{
    if (!s_bReady)
    {
        return;
    }

    VideoScreenVBL ();

    // And then tell Mac OS that a frame went by. This is not decoration: the
    // Macintosh's own video driver registered a VBL service when it opened
    // (video.cpp:187), and the Cursor Device Manager's work is queued behind
    // it. Every upstream platform ends VideoVBL() with exactly these two lines
    // — video_x.cpp:2192, video_beos.cpp:410 — and leaving them out is why the
    // pointer would not move here: the position reached the cursor device and
    // then waited for a service call that never came, so Mouse and RawMouse
    // stayed where they were. The keyboard and the mouse buttons were fine
    // throughout, because neither goes this way.
    if (private_data != 0 && private_data->interruptsEnabled)
    {
        VSLDoInterruptService (private_data->vslServiceID);
    }
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

    // mac_pal is an array of structs and the shared layer wants triplets, which
    // is also the shape Basilisk's core hands over — so the conversion is here,
    // where the difference is, and nowhere else.
    const int nIn = PaletteSizeOf (VModes[cur_mode].viAppleMode);
    uint8 Palette[256 * 3];
    for (int c = 0; c < nIn && c < 256; c++)
    {
        Palette[c * 3 + 0] = mac_pal[c].red;
        Palette[c * 3 + 1] = mac_pal[c].green;
        Palette[c * 3 + 2] = mac_pal[c].blue;
    }
    VideoScreenPalette (Palette, (unsigned) nIn);
}

void video_set_gamma (int n_colors)
{
    // The core applies gamma to the palette before calling video_set_palette,
    // so indexed modes are already handled. A direct mode would need a
    // per-pixel table, and it would have to invalidate the screen as well: it
    // changes what is shown without changing a single guest byte.
    (void) n_colors;
}

void video_set_dirty_area (int x, int y, int w, int h)
{
    // The Macintosh saying what it changed, which is better information than a
    // comparison can recover — and the reason the dirty-tile work is cheaper on
    // this engine than on the other. Announced tiles are redrawn without being
    // compared; the rest are still compared, because this is called from the
    // accelerated paths only (gfxaccel.cpp:63) and is a hint rather than the
    // whole story.
    VideoScreenAnnounce (x, y, w, h);
}

int16 video_mode_change (VidLocals *csSave, uint32 ParamPtr)
{
    // Nothing to do if it is the mode already in effect. Upstream tests this
    // first for the same reason: the driver asks whenever a window opens.
    if (csSave->saveData == ReadMacInt32 (ParamPtr + csData)
        && csSave->saveMode == ReadMacInt16 (ParamPtr + csMode))
    {
        return noErr;
    }

    for (unsigned i = 0; VModes[i].viType != DIS_INVALID; i++)
    {
        if (ReadMacInt16 (ParamPtr + csMode) != VModes[i].viAppleMode
            || ReadMacInt32 (ParamPtr + csData) != VModes[i].viAppleID)
        {
            continue;
        }

        // The Macintosh must not take an interrupt while the mode is half
        // changed: the tick would call VideoVBL and composite a buffer whose
        // size no longer matches the one the compositor was planned for.
        DisableInterrupt ();
        const bool bOK = SwitchTo (i);
        EnableInterrupt ();

        if (!bOK)
        {
            return paramErr;
        }

        csSave->saveMode     = ReadMacInt16 (ParamPtr + csMode);
        csSave->saveData     = ReadMacInt32 (ParamPtr + csData);
        csSave->savePage     = ReadMacInt16 (ParamPtr + csPage);
        csSave->saveBaseAddr = screen_base;
        WriteMacInt32 (ParamPtr + csBaseAddr, screen_base);
        return noErr;
    }

    // A mode we do not offer. paramErr is what the driver expects and what
    // makes the Monitors control panel leave the setting alone. Said out loud,
    // because a refusal here is how a Macintosh ends up drawing at a size the
    // screen is not showing, and that reads as a compositor fault.
    CLogger::Get ()->Write (FROM, LogWarning,
                            "Mode change refused: the Mac asked for mode %04x, id %08x",
                            (unsigned) ReadMacInt16 (ParamPtr + csMode),
                            (unsigned) ReadMacInt32 (ParamPtr + csData));
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

/*
 *  What the input bridge needs to know
 *
 *  This engine feeds the Macintosh an absolute pointer position rather than the
 *  deltas the other one sends, so the bridge has to know how far the pointer
 *  may go. Answered here because the mode is this file's business, and it moves
 *  with the mode.
 */
void VideoMacScreenSize (unsigned *pWidth, unsigned *pHeight)
{
    if (s_bReady)
    {
        *pWidth  = VModes[cur_mode].viXsize;
        *pHeight = VModes[cur_mode].viYsize;
    }
}

// VideoActivated() and VideoSnapshot() are video.cpp's own, not the platform's,
// however video.h reads. Defining them here was a guess from the header, and
// the linker corrected it — which is the point of getting to a link early.
