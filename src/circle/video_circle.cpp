/*
 * video_circle.cpp — Okapia platform layer: the Mac's screen.
 *
 * The Mac owns a frame buffer in RAM; Okapia owns one fixed frame buffer on the
 * HDMI output and composites between them. The two never have to match: the
 * emulated video card is virtual, so its modes are whatever we declare, and on
 * the Raspberry Pi 5 the output resolution cannot be chosen anyway (§7.2).
 *
 * Changing resolution or depth from the Monitors control panel therefore costs
 * a reallocation and a compositor reconfiguration — no HDMI renegotiation, no
 * blanking.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include <circle/bcmframebuffer.h>
#include <string.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "video.h"
#include "video_defs.h"
#include "video_blit.h"

#define FROM "okapia-video"

// Composite one VBL in this many. 1 is every frame; higher trades refresh rate
// for guest speed. Measured under QEMU, 1 left the Mac at a seventh of its
// proper pace.
// Upstream exposes this as the "frameskip" preference — the same scale as the
// Window Refresh Rate menu in Basilisk (60 Hz = 1, 30 = 2, 15 = 4, 10 = 6,
// 7.5 = 8, 5 = 12), with 0 meaning Dynamic. Its default is 6, and hardcoding
// that here left the screen refreshing 9 times a second; the cursor is drawn by
// the Mac into its own framebuffer, so it inherited that rate and looked like a
// laggy mouse. Compositing every VBL measures at 9.8% of wall time, which we can
// afford, so the default here is 1. Dynamic is not implemented yet (phase 12).
static unsigned s_nFrameSkip = 6;

// frameskip 0 means Dynamic: hold the compositor to a share of wall time and let
// the rate follow whatever the machine actually costs. That matters more than it
// sounds — the same composite takes 1.5 ms headless and 74 ms with a QEMU window
// attached, because QEMU then tracks dirty pages on the frame buffer. A fixed
// rate that is comfortable in one case starves the guest in the other.
static bool s_bDynamic;

// video.h has DepthModeForPixelDepth but no inverse; this is the one we need.
static inline int DepthBits (video_depth depth)
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

/*
 *  Output side: one frame buffer, claimed once, never resized.
 */

static CBcmFrameBuffer *s_pOutput;
static uint8  *s_pOutputPixels;
static unsigned s_nOutputWidth, s_nOutputHeight, s_nOutputDepth, s_nOutputPitch;

/*
 *  Guest side: the buffer Mac OS draws into.
 */

static uint8  *s_pMacPixels;
static uint32  s_nMacBufferSize;

/*
 *  Compositor placement, recomputed on every mode switch.
 */

static unsigned s_nScale;        // integer scale factor, at least 1
static unsigned s_nOriginX;      // top-left of the Mac image in the output
static unsigned s_nOriginY;

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

    bool allocate_mac_frame_buffer (const video_mode &mode);
    void composite (void);
};

static Circle_monitor_desc *s_pMonitor;

/*
 *  Mode list
 *
 *  We decide what the Monitors control panel offers. Only modes whose doubled
 *  size still fits the output are worth offering: integer scaling is what keeps
 *  the image sharp, and a non-integer factor looks worse than a smaller picture.
 */

static void add_mode (vector<video_mode> &modes, uint32 width, uint32 height,
                      uint32 resolution_id, video_depth depth)
{
    if (width > s_nOutputWidth || height > s_nOutputHeight)
    {
        return;                 // would not even fit unscaled
    }

    video_mode mode;
    mode.x             = width;
    mode.y             = height;
    mode.resolution_id = resolution_id;
    mode.depth         = depth;
    mode.bytes_per_row = TrivialBytesPerRow (width, depth);
    mode.user_data     = 0;
    modes.push_back (mode);
}

static void build_mode_list (vector<video_mode> &modes)
{
    static const struct { uint32 x, y, id; } sizes[] = {
        { 512, 384, 0x80 },
        { 640, 480, 0x81 },
        { 800, 600, 0x82 },
        { 1024, 768, 0x83 },
    };

    // 1, 2, 4 and 8 bits are indexed; grey and black-and-white are the same modes
    // with a different palette, which the core computes for us (video.cpp:569).
    // Only 8 bits for now. Offering 1-bit as well let the Mac paint in 1 bit —
    // 80 bytes per row — while the compositor read 640, which put the startup
    // icon in the wrong place. Depth switching comes back once the compositor
    // follows the guest's choice properly.
    static const video_depth depths[] = { VDEPTH_8BIT };

    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
    {
        for (unsigned d = 0; d < sizeof depths / sizeof depths[0]; d++)
        {
            add_mode (modes, sizes[i].x, sizes[i].y, sizes[i].id, depths[d]);
        }
    }
}

/*
 *  Guest buffer and compositor placement
 */

bool Circle_monitor_desc::allocate_mac_frame_buffer (const video_mode &mode)
{
    uint32 nSize = mode.bytes_per_row * mode.y;

    if (nSize > s_nMacBufferSize)
    {
        // Allocated once at the largest mode, so a switch never allocates in a
        // running emulation.
        CLogger::Get ()->Write (FROM, LogError,
                                "Mode needs %u KB, buffer holds %u KB",
                                (unsigned) (nSize / 1024),
                                (unsigned) (s_nMacBufferSize / 1024));
        return false;
    }

    memset (s_pMacPixels, 0, nSize);
    set_mac_frame_base (Host2MacAddr (s_pMacPixels));

    // Largest integer factor that still fits, then centre what we get.
    s_nScale = 1;
    while (   mode.x * (s_nScale + 1) <= s_nOutputWidth
           && mode.y * (s_nScale + 1) <= s_nOutputHeight)
    {
        s_nScale++;
    }
    s_nOriginX = (s_nOutputWidth  - mode.x * s_nScale) / 2;
    s_nOriginY = (s_nOutputHeight - mode.y * s_nScale) / 2;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac mode %ux%u %u bpp, shown at %ux scale, origin %u,%u",
                            (unsigned) mode.x, (unsigned) mode.y,
                            (unsigned) DepthBits (mode.depth),
                            s_nScale, s_nOriginX, s_nOriginY);
    return true;
}

void Circle_monitor_desc::switch_to_current_mode (void)
{
    const video_mode &mode = get_current_mode ();

    if (!allocate_mac_frame_buffer (mode))
    {
        ErrorAlert (GetString (STR_OPEN_WINDOW_ERR));
        return;
    }

    // Pick the conversion routine for this depth and the output's pixel format.
    // video_blit.cpp already handles the Mac's big-endian layout.
    VisualFormat visual;
    visual.fullscreen = true;
    visual.depth      = s_nOutputDepth;
    visual.Rmask      = 0x00FF0000;
    visual.Gmask      = 0x0000FF00;
    visual.Bmask      = 0x000000FF;
    visual.Rshift     = 16;
    visual.Gshift     = 8;
    visual.Bshift     = 0;
    Screen_blitter_init (visual, true, DepthBits (mode.depth));
}

/*
 *  Palette
 *
 *  In indexed modes the core hands us the palette already gamma-corrected and,
 *  when the Mac asks for greyscale, already reduced to greys. We turn it into
 *  the lookup table the blitter reads.
 */

void Circle_monitor_desc::set_palette (uint8 *pal, int num)
{
    const video_mode &mode = get_current_mode ();
    if (IsDirectMode (mode))
    {
        return;                 // no palette in 16- or 32-bit modes
    }

    for (int i = 0; i < 256; i++)
    {
        int c = i & (num - 1);  // repeat when fewer than 256 entries, as SDL does
        ExpandMap[i] = 0xFF000000
                     | ((uint32) pal[c * 3 + 0] << 16)
                     | ((uint32) pal[c * 3 + 1] << 8)
                     |  (uint32) pal[c * 3 + 2];
    }
}

void Circle_monitor_desc::set_gamma (uint8 *gamma, int num)
{
    // The core applies gamma to the palette before calling set_palette, so
    // indexed modes are already handled. Direct modes would need a per-pixel
    // table; nothing asks for it yet.
}

/*
 *  Compositor: Mac buffer -> output frame buffer.
 *
 *  Row by row, never pixel by pixel with a function call. Screen_blit converts
 *  one row into the output format; the scale factor then repeats it.
 */

// Phase 11 wants the cost of compositing, not a guess. CLOCKHZ is 1 MHz, so
// these are microseconds; the pair is read by the report in VideoInterrupt().
static u64 s_nCompositeUsec;
static unsigned s_nComposites;

void Circle_monitor_desc::composite (void)
{
    const unsigned nStart = CTimer::GetClockTicks ();
    const video_mode &mode = get_current_mode ();
    const unsigned nBytesPerOutputPixel = s_nOutputDepth / 8;

    static uint8 RowBuffer[4096 * 4];
    if (mode.x * nBytesPerOutputPixel > sizeof RowBuffer)
    {
        return;
    }

    for (unsigned y = 0; y < mode.y; y++)
    {
        const uint8 *pSrc = s_pMacPixels + y * mode.bytes_per_row;
        Screen_blit (RowBuffer, pSrc, mode.x);

        // Horizontal scale into the output row, then repeat it vertically.
        uint8 *pDst = s_pOutputPixels
                    + (s_nOriginY + y * s_nScale) * s_nOutputPitch
                    + s_nOriginX * nBytesPerOutputPixel;

        if (s_nScale == 1)
        {
            memcpy (pDst, RowBuffer, mode.x * nBytesPerOutputPixel);
        }
        else if (s_nScale == 2 && ((uintptr) pDst & 7) == 0)
        {
            // Doubling is the common case and the scalar loop below costs one
            // store per output pixel. Two output pixels are one 64-bit store,
            // which halves them; the output pitch is a multiple of 8, so the
            // alignment only has to be checked once per row.
            u64 *pOut = (u64 *) pDst;
            const uint32 *pIn = (const uint32 *) RowBuffer;
            for (unsigned x = 0; x < mode.x; x++)
            {
                u64 v = pIn[x];
                pOut[x] = v | (v << 32);
            }
        }
        else
        {
            uint32 *pOut = (uint32 *) pDst;
            const uint32 *pIn = (const uint32 *) RowBuffer;
            for (unsigned x = 0; x < mode.x; x++)
            {
                for (unsigned s = 0; s < s_nScale; s++)
                {
                    *pOut++ = pIn[x];
                }
            }
        }

        for (unsigned s = 1; s < s_nScale; s++)
        {
            memcpy (pDst + s * s_nOutputPitch, pDst, mode.x * s_nScale * nBytesPerOutputPixel);
        }
    }

    s_nCompositeUsec += (unsigned) (CTimer::GetClockTicks () - nStart);
    s_nComposites++;
}

/*
 *  Entry points the core calls
 */

bool VideoInit (bool classic)
{
    // Claim the output. Ask for what we want; report what the firmware granted,
    // because on the Pi 5 the request is ignored.
    s_pOutput = new CBcmFrameBuffer (0, 0, 32);      // 0,0 = the display's own size
    if (!s_pOutput->Initialize ())
    {
        CLogger::Get ()->Write (FROM, LogError, "No frame buffer");
        return false;
    }

    // 0 means Dynamic upstream: a 16x16 box grid, refreshed at a rate that
    // follows how much actually changed (video_x.cpp:2343). We do not have it
    // yet, so say so rather than silently behaving like something else.
    int32 nSkip = PrefsFindInt32 ("frameskip");
    s_bDynamic = (nSkip <= 0);
    s_nFrameSkip = s_bDynamic ? 6 : (unsigned) nSkip;   // 6 until the first measure

    s_nOutputWidth  = s_pOutput->GetWidth ();
    s_nOutputHeight = s_pOutput->GetHeight ();
    s_nOutputDepth  = s_pOutput->GetDepth ();
    s_nOutputPitch  = s_pOutput->GetPitch ();
    s_pOutputPixels = (uint8 *) (uintptr) s_pOutput->GetBuffer ();

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Output: %ux%u, %u bpp, pitch %u",
                            s_nOutputWidth, s_nOutputHeight, s_nOutputDepth, s_nOutputPitch);

    if (s_nOutputDepth != 32)
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "Expected a 32 bpp output, got %u", s_nOutputDepth);
        return false;
    }

    vector<video_mode> modes;
    build_mode_list (modes);
    if (modes.empty ())
    {
        CLogger::Get ()->Write (FROM, LogError,
                                "No Mac mode fits a %ux%u output",
                                s_nOutputWidth, s_nOutputHeight);
        return false;
    }

    // One buffer, sized for the largest mode, so switching never allocates.
    s_nMacBufferSize = 0;
    for (unsigned i = 0; i < modes.size (); i++)
    {
        uint32 nSize = modes[i].bytes_per_row * modes[i].y;
        if (nSize > s_nMacBufferSize)
        {
            s_nMacBufferSize = nSize;
        }
    }
    s_pMacPixels = (uint8 *) CMemorySystem::HeapAllocate (s_nMacBufferSize, HEAP_ANY);
    if (s_pMacPixels == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot allocate the Mac frame buffer");
        return false;
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
    delete s_pOutput;
    s_pOutput = 0;
}

/*
 *  Called at the Mac's vertical blank. This is where a frame reaches the screen.
 */

void VideoInterrupt (void)
{
    if (s_pMonitor == 0)
    {
        return;
    }

    // The compositor runs inside the Mac's VBL, so its cost is taken straight
    // out of the guest's execution time. Converting and scaling 640x480 on every
    // interrupt starved the emulation: the Mac was servicing about 8 VBLs a
    // second instead of 60. Compositing every Nth interrupt gives the time back.
    static unsigned s_nSkip;
    if (++s_nSkip >= s_nFrameSkip)
    {
        s_nSkip = 0;
        s_pMonitor->composite ();
    }

    // Diagnosis for early bring-up: is the guest drawing at all? A frame buffer
    // that stays blank means the Mac never got as far as its first pixel, which
    // looks identical to a working compositor with nothing to show.
    static unsigned s_nFrames;
    static unsigned s_nLastReport;
    s_nFrames++;

    unsigned nNow = CTimer::Get ()->GetTicks () / HZ;
    if (nNow != s_nLastReport && (nNow % 5) == 0)
    {
        s_nLastReport = nNow;

        unsigned nNonZero = 0;
        const video_mode &mode = s_pMonitor->get_current_mode ();
        for (uint32 i = 0; i < mode.bytes_per_row * mode.y; i += 997)   // sparse probe
        {
            if (s_pMacPixels[i] != 0)
            {
                nNonZero++;
            }
        }

        // s_nFrames counts VBLs, not composites — the screen is only refreshed
        // every VIDEO_COMPOSITE_EVERY of them, and reporting the VBL rate as
        // "fps" hid a 9 Hz display behind a reassuring 55.
        unsigned nPerComposite = s_nComposites
                               ? (unsigned) (s_nCompositeUsec / s_nComposites) : 0;

        // Dynamic: keep compositing to about an eighth of wall time. A VBL is
        // 16667 us, so a composite costing C us fits in ceil(C * 8 / 16667)
        // of them. Capped, because past a point the screen is a slideshow and
        // the answer is phase 12's dirty regions, not a slower rate.
        if (s_bDynamic && nPerComposite > 0)
        {
            unsigned nWanted = (nPerComposite * 8) / 16667 + 1;
            if (nWanted > 12)
            {
                nWanted = 12;
            }
            if (nWanted != s_nFrameSkip)
            {
                CLogger::Get ()->Write (FROM, LogNotice,
                                        "dynamic: composite %u us, refresh every %u VBL",
                                        nPerComposite, nWanted);
                s_nFrameSkip = nWanted;
            }
        }
        unsigned nLoadPerMille = nNow
                               ? (unsigned) (s_nCompositeUsec / nNow / 1000) : 0;

        CLogger::Get ()->Write (FROM, LogNotice,
                                "%u VBL (%u/s), screen %u/s, composite %u us "
                                "(%u.%u%% of wall), guest buffer %s",
                                s_nFrames, s_nFrames / (nNow ? nNow : 1),
                                s_nComposites / (nNow ? nNow : 1),
                                nPerComposite,
                                nLoadPerMille / 10, nLoadPerMille % 10,
                                nNonZero > 0 ? "has content" : "still blank");
    }
}

void VideoQuitFullScreen (void)
{
    // Okapia is always full screen; nothing to leave.
}
