/*
 * video_circle.cpp — Basilisk's screen, on a Raspberry Pi.
 *
 * The Mac owns a frame buffer in RAM; Okapia owns one fixed frame buffer on the
 * HDMI output and composites between them. The two never have to match: the
 * emulated video card is virtual, so its modes are whatever we declare, and on
 * the Raspberry Pi 5 the output resolution cannot be chosen anyway (§7.2).
 *
 * What is left here is this engine's contract and nothing else: Basilisk hands
 * the platform a monitor_desc and asks it to describe modes. Everything between
 * that contract and the compositor — which modes fit, which converter a depth
 * needs, the palette, the refresh rate, the reporting — is in
 * video_shared_circle.cpp, because SheepShaver asks the same questions and the
 * two copies had already drifted.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include <string.h>

#include "video_shared_circle.h"

#include "cpu_emulation.h"
#include "main.h"
#include "user_strings.h"
#include "video.h"
#include "video_defs.h"

#define FROM "okapia-video"

// The guest's frame buffer. Allocated from the heap, once for the life of the
// board and at the largest mode offered, so a mode change never allocates and a
// restart never takes a second one.
static uint8 *s_pMacPixels;
static uint32 s_nMacBufferSize;

// video.h has DepthModeForPixelDepth but no inverse; this is the one we need.
static inline unsigned DepthBits (video_depth depth)
{
    switch (depth)
    {
    case VDEPTH_1BIT:  return 1;
    case VDEPTH_2BIT:  return 2;
    case VDEPTH_4BIT:  return 4;
    case VDEPTH_8BIT:  return 8;
    case VDEPTH_16BIT: return 16;
    case VDEPTH_32BIT: return 32;
    default:           return 8;
    }
}

static inline video_depth DepthOf (unsigned nBits)
{
    switch (nBits)
    {
    case 1:  return VDEPTH_1BIT;
    case 2:  return VDEPTH_2BIT;
    case 4:  return VDEPTH_4BIT;
    case 16: return VDEPTH_16BIT;
    case 32: return VDEPTH_32BIT;
    default: return VDEPTH_8BIT;
    }
}

class Circle_monitor_desc : public monitor_desc
{
public:
    Circle_monitor_desc (const vector<video_mode> &modes, video_depth default_depth,
                         uint32 default_id)
    :   monitor_desc (modes, default_depth, default_id)
    {
    }

    void switch_to_current_mode (void);
    void set_palette (uint8 *pal, int num);
    void set_gamma (uint8 *gamma, int num);
};

static Circle_monitor_desc *s_pMonitor;

/*
 *  Mode list
 *
 *  The sizes are this engine's — 512x384 is a Classic resolution SheepShaver
 *  has no identifier for — and so are the resolution_ids. Which of them survive
 *  is not: VideoScreenEnumerate applies the same rules to both engines.
 */

static const TVideoScreenSize SIZES[] =
{
    { 512,  384,  0x80 },
    { 640,  480,  0x81 },
    { 800,  600,  0x82 },
    { 1024, 768,  0x83 },
};

// See VideoScreenEnumerate: a bigger guest buffer costs more than the pixels it
// holds, so what is offered is capped rather than what is drawn.
static const uint32 MAX_BUFFER = 1536 * 1024;

static void AddMode (void *pContext, const TVideoScreenSize *pSize,
                     unsigned nBits, unsigned nBytesPerRow)
{
    vector<video_mode> *pModes = (vector<video_mode> *) pContext;

    video_mode mode;
    mode.x             = pSize->nWidth;
    mode.y             = pSize->nHeight;
    mode.resolution_id = pSize->nId;
    mode.depth         = DepthOf (nBits);
    mode.bytes_per_row = nBytesPerRow;
    mode.user_data     = 0;
    pModes->push_back (mode);
}

/*
 *  What the core asks of a monitor
 */

void Circle_monitor_desc::switch_to_current_mode (void)
{
    const video_mode &mode = get_current_mode ();

    memset (s_pMacPixels, 0, (size_t) mode.bytes_per_row * mode.y);
    set_mac_frame_base (Host2MacAddr (s_pMacPixels));

    if (!VideoScreenApply (s_pMacPixels, mode.x, mode.y,
                           mode.bytes_per_row, DepthBits (mode.depth)))
    {
        ErrorAlert (GetString (STR_OPEN_WINDOW_ERR));
    }
}

void Circle_monitor_desc::set_palette (uint8 *pal, int num)
{
    if (IsDirectMode (get_current_mode ()))
    {
        return;                 // no palette in 16- or 32-bit modes
    }

    // The core hands it over already gamma-corrected and, when the Mac asks for
    // greyscale, already reduced to greys — as red-green-blue triplets, which
    // is exactly what the shared layer wants.
    VideoScreenPalette (pal, (unsigned) num);
}

void Circle_monitor_desc::set_gamma (uint8 *gamma, int num)
{
    // The core applies gamma to the palette before calling set_palette, so
    // indexed modes are already handled. Direct modes would need a per-pixel
    // table, and it would have to invalidate the screen as well: it changes what
    // is shown without changing a single guest byte.
}

/*
 *  Entry points the core calls
 */

bool VideoInit (bool classic)
{
    // The output first: which modes fit is a question about its geometry.
    if (!VideoScreenOpen ())
    {
        return false;
    }
    VideoScreenReadPrefs ();

    vector<video_mode> modes;
    VideoScreenEnumerate (SIZES, sizeof SIZES / sizeof SIZES[0], MAX_BUFFER,
                          AddMode, &modes);
    if (modes.empty ())
    {
        CLogger::Get ()->Write (FROM, LogError, "No Mac mode fits this output");
        return false;
    }

    // One buffer, sized for the largest mode, so switching never allocates.
    // The shadow is the same size, for the same reason.
    s_nMacBufferSize = 0;
    for (unsigned i = 0; i < modes.size (); i++)
    {
        const uint32 nSize = modes[i].bytes_per_row * modes[i].y;
        if (nSize > s_nMacBufferSize)
        {
            s_nMacBufferSize = nSize;
        }
    }
    if (!VideoScreenShadow (s_nMacBufferSize))
    {
        return false;
    }

    // Once for the life of the board, like the shadow: a restart from Mac OS
    // comes back through here.
    if (s_pMacPixels == 0)
    {
        s_pMacPixels = (uint8 *) CMemorySystem::HeapAllocate (s_nMacBufferSize, HEAP_ANY);
        if (s_pMacPixels == 0)
        {
            CLogger::Get ()->Write (FROM, LogError, "Cannot allocate the Mac frame buffer");
            return false;
        }
    }

    CLogger::Get ()->Write (FROM, LogNotice, "%u modes offered, %u KB guest buffer",
                            (unsigned) modes.size (), (unsigned) (s_nMacBufferSize / 1024));

    s_pMonitor = new Circle_monitor_desc (modes, VDEPTH_8BIT, 0x81);   // 640x480x8
    VideoMonitors.push_back (s_pMonitor);
    s_pMonitor->switch_to_current_mode ();
    return true;
}

void VideoExit (void)
{
    // VideoInit() added a monitor to the list and upstream never takes one out
    // again — it exits the process next and has no reason to. Okapia goes round
    // instead, and a second entry is a second screen: InitAll() reads
    // VideoMonitors[0] (main.cpp:180) and the Mac's video driver opens the
    // monitor from the previous life, so the picture is drawn into a buffer
    // nothing composites. That is exactly what a restart looked like — a grey
    // field under System 7.1, a wallpaper landing half off the screen under 7.6.
    for (unsigned i = 0; i < VideoMonitors.size (); i++)
    {
        if (VideoMonitors[i] == s_pMonitor)
        {
            VideoMonitors.erase (VideoMonitors.begin () + i);
            break;
        }
    }
    delete s_pMonitor;
    s_pMonitor = 0;

    // The frame buffers stay: see VideoInit().
    VideoScreenClose ();
}

/*
 *  Called at the Mac's vertical blank. This is where a frame reaches the screen.
 */

void VideoInterrupt (void)
{
    if (s_pMonitor != 0)
    {
        VideoScreenVBL ();
    }
}

void VideoQuitFullScreen (void)
{
    // Okapia is always full screen; nothing to leave.
}
