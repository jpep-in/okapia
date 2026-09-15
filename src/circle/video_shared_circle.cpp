/*
 * video_shared_circle.cpp — see video_shared_circle.h.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"

#include <circle/bcmframebuffer.h>
#include <stdio.h>
#include <string.h>

#include "okapia_output.h"
#include "hal_circle.h"
#include "compositor_circle.h"
#include "video_shared_circle.h"

#include "prefs.h"
// video.h before video_blit.h: the latter names the depth enumeration and each
// tree declares its own. Nothing else in this file touches either — the point
// of it is that neither engine's vocabulary gets in.
#include "video.h"
#include "video_blit.h"

#define FROM "okapia-video"

const unsigned VideoScreenDepths[6] = { 1, 2, 4, 8, 16, 32 };

static CBcmFrameBuffer *s_pOutput;
static bool             s_bRedLow;      // FwOutputRedLow(), read at each open
static u8              *s_pShadow;
static u32              s_nShadowBytes;
static TCompositor      s_Compositor;
static bool             s_bReady;

// Composite one VBL in this many. Upstream exposes it as "frameskip", on the
// same scale as Basilisk's Window Refresh Rate menu — 60 Hz = 1, 30 = 2,
// 15 = 4, 10 = 6, 7.5 = 8, 5 = 12 — and 0 means Dynamic.
//
// Honour the preference and never hardcode the rate. Upstream's default of 6
// showed up here as a 9 Hz display and a mouse that dragged, because the Mac
// draws its own cursor into its own frame buffer and the pointer can never move
// more often than the compositor runs.
static unsigned s_nFrameSkip = 1;
static bool     s_bDynamic;
static unsigned s_nVBLsSinceComposite;
static unsigned s_nVBLs;
static unsigned s_nLastReport;
static unsigned s_nLastWindow;

unsigned VideoScreenRowBytes (unsigned nWidth, unsigned nBits)
{
    switch (nBits)
    {
    case 1:  return (nWidth + 7) / 8;
    case 2:  return (nWidth + 3) / 4;
    case 4:  return (nWidth + 1) / 2;
    case 16: return nWidth * 2;
    case 32: return nWidth * 4;
    default: return nWidth;
    }
}

/*
 *  Opening
 */

bool VideoScreenOpen (void)
{
    // Borrowed, not claimed: the firmware holds the one claim for the life of
    // the board (okapia_output.h). Taking a frame buffer of our own here would
    // mean a mailbox transaction at every handover, and there is one at every
    // restart from Mac OS — the display changes hands twice per round.
    s_pOutput = FwOutputClaim ();
    if (s_pOutput == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "No frame buffer");
        return false;
    }

    s_bRedLow = FwOutputRedLow ();

    if (s_pOutput->GetDepth () != 32)
    {
        CLogger::Get ()->Write (FROM, LogError, "Expected a 32 bpp output, got %u",
                                s_pOutput->GetDepth ());
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Output: %ux%u, %u bpp, pitch %u",
                            s_pOutput->GetWidth (), s_pOutput->GetHeight (),
                            s_pOutput->GetDepth (), s_pOutput->GetPitch ());

    // Nothing on the output belongs to this Macintosh yet: the firmware drew a
    // whole screen of its own between the two.
    s_Compositor.bFullRedraw = true;
    s_nVBLs = 0;
    return true;
}

bool VideoScreenShadow (u32 nBytes)
{
    // Once for the life of the board, not once per start. A restart from Mac OS
    // comes back through here, and a block this size taken on every round would
    // eat the board's memory a Macintosh at a time. The output cannot change
    // size between rounds, so the first shadow is always big enough — but say so
    // rather than trust it.
    if (s_pShadow != 0)
    {
        if (nBytes > s_nShadowBytes)
        {
            CLogger::Get ()->Write (FROM, LogError,
                                    "The guest frame grew from %u to %u KB between starts",
                                    (unsigned) (s_nShadowBytes / 1024),
                                    (unsigned) (nBytes / 1024));
            return false;
        }
        return true;
    }

    s_pShadow = (u8 *) CMemorySystem::HeapAllocate (nBytes, HEAP_ANY);
    if (s_pShadow == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot allocate the compositor shadow");
        return false;
    }
    s_nShadowBytes = nBytes;
    return true;
}

void VideoScreenClose (void)
{
    // The output was never ours to release, and the shadow stays: see above.
    s_bReady = false;
}

unsigned VideoScreenOutputWidth (void)
{
    return s_pOutput != 0 ? s_pOutput->GetWidth () : 0;
}

unsigned VideoScreenOutputHeight (void)
{
    return s_pOutput != 0 ? s_pOutput->GetHeight () : 0;
}

u32 VideoScreenShadowBytes (void)
{
    return s_nShadowBytes;
}

/*
 *  The mode list
 *
 *  Only sizes that fit the output unscaled are worth offering: integer scaling
 *  is what keeps the image sharp, and a non-integer factor looks worse than a
 *  smaller picture.
 *
 *  Each engine caps the guest frame it offers, and the cap is what sizes the
 *  buffer and the shadow. A 5x composite cost was once measured at 640x480x8
 *  with a 3 MB buffer against a 1.2 MB one, and caps were kept small for it; it
 *  did not come back (QEMU, 1280x960 output, 2026-09-13: 71-156 us at rest with
 *  4.8 MB, 247 us with 1.5 MB), so the caps now follow the largest display.
 */

void VideoScreenEnumerate (const TVideoScreenSize *pSizes, unsigned nSizes,
                           u32 nMaxBufferBytes,
                           TVideoScreenSink *pSink, void *pContext)
{
    const unsigned nOutW = VideoScreenOutputWidth ();
    const unsigned nOutH = VideoScreenOutputHeight ();

    unsigned nModes = 0;
    char Names[256];
    unsigned nAt = 0;
    Names[0] = '\0';
    for (unsigned i = 0; i < nSizes; i++)
    {
        if (pSizes[i].nWidth > nOutW || pSizes[i].nHeight > nOutH)
        {
            continue;
        }
        unsigned nDepths = 0;
        for (unsigned d = 0; d < VIDEO_SCREEN_DEPTHS; d++)
        {
            const unsigned nBits = VideoScreenDepths[d];
            const unsigned nRow  = VideoScreenRowBytes (pSizes[i].nWidth, nBits);
            if ((u32) nRow * pSizes[i].nHeight > nMaxBufferBytes)
            {
                continue;
            }
            pSink (pContext, &pSizes[i], nBits, nRow);
            nDepths++;
        }
        if (nDepths != 0 && nAt < sizeof Names)
        {
            nAt += (unsigned) snprintf (Names + nAt, sizeof Names - nAt, " %ux%u",
                                        pSizes[i].nWidth, pSizes[i].nHeight);
        }
        nModes += nDepths;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "%u modes offered within %u KB, sizes%s",
                            nModes, (unsigned) (nMaxBufferBytes / 1024), Names);
}

unsigned VideoScreenSizes (const u32 *pStandardIds, const u32 *pExtraIds,
                           TVideoScreenSize *pOut,
                           unsigned *pWantW, unsigned *pWantH)
{
    // Sizes that fill the display at twice or three times were offered once,
    // and came out as 912x492 and 853x480: nobody's resolution. The display's
    // own size is the one size that exists because of this display.
    unsigned nDisplayW = VideoScreenOutputWidth ();
    unsigned nDisplayH = VideoScreenOutputHeight ();
    if (nDisplayW < 640 || nDisplayH < 480)
    {
        nDisplayW = nDisplayH = 0;
    }

    *pWantW = 640;
    *pWantH = 480;
    unsigned nPrefW = 0, nPrefH = 0;
    const char *pValue = PrefsFindString ("screen");
    int w, h;
    if (pValue != 0 && sscanf (pValue, "%*[^/]/%d/%d", &w, &h) == 2 && w >= 0 && h >= 0)
    {
        nPrefW = w > 0 ? (unsigned) w : VideoScreenOutputWidth ();
        nPrefH = h > 0 ? (unsigned) h : VideoScreenOutputHeight ();
        *pWantW = nPrefW;
        *pWantH = nPrefH;
    }

    return VideoScreenSizeList (pStandardIds, pExtraIds, nDisplayW, nDisplayH,
                                nPrefW, nPrefH, pOut);
}

/*
 *  Putting a mode into effect
 */

bool VideoScreenApply (const u8 *pSource, unsigned nWidth, unsigned nHeight,
                       unsigned nBytesPerRow, unsigned nBits)
{
    if (s_pOutput == 0 || pSource == 0)
    {
        return false;
    }

    const u32 nNeeded = (u32) nBytesPerRow * nHeight;
    if (nNeeded > s_nShadowBytes)
    {
        CLogger::Get ()->Write (FROM, LogError, "Mode needs %u KB, the shadow holds %u KB",
                                (unsigned) (nNeeded / 1024),
                                (unsigned) (s_nShadowBytes / 1024));
        return false;
    }

    // The output's pixel format, which is what video_blit.cpp converts into,
    // and which the firmware states (okapia_output.h). It was once fixed at red
    // in the low byte, from an argument about Circle's SetPalette that holds for
    // 8-bit palettes and says nothing about a 32-bit frame buffer: that matched
    // QEMU and exchanged red and blue on every real board.
    //
    // fullscreen is not decoration either: without it Screen_blitter_init sends
    // the one-bit depth down an X11 path that assumes a 1-bit image
    // (video_blit.cpp:533).
    VisualFormat Visual;
    Visual.fullscreen = true;
    Visual.depth      = (int) s_pOutput->GetDepth ();
    Visual.Rmask      = s_bRedLow ? 0x000000FF : 0x00FF0000;
    Visual.Gmask      = 0x0000FF00;
    Visual.Bmask      = s_bRedLow ? 0x00FF0000 : 0x000000FF;
    Visual.Rshift     = s_bRedLow ? 0 : 16;
    Visual.Gshift     = 8;
    Visual.Bshift     = s_bRedLow ? 16 : 0;

    if (nBits == 16 || nBits == 32)
    {
        // Not Screen_blit: its direct-mode blitters assume the host reads
        // pixels the way the Mac wrote them (compositor_circle.h). Handing it a
        // direct mode gave a screen entirely in one colour at 16 bits and
        // vertical stripes at 32.
        if (s_bRedLow)
        {
            s_Compositor.pConvertRow = (nBits == 16) ? CompositorConvert16To32
                                                     : CompositorConvert32To32;
        }
        else
        {
            s_Compositor.pConvertRow = (nBits == 16) ? CompositorConvert16To32Bgr
                                                     : CompositorConvert32To32Bgr;
        }
    }
    else
    {
        Screen_blitter_init (Visual, true, (int) nBits);
        s_Compositor.pConvertRow = Screen_blit;
    }

    memset (s_pShadow, 0, nNeeded);

    s_Compositor.pSource       = pSource;
    s_Compositor.nWidth        = nWidth;
    s_Compositor.nHeight       = nHeight;
    s_Compositor.nBytesPerRow  = nBytesPerRow;
    s_Compositor.nSourceBits   = nBits;
    s_Compositor.pOutput       = (u8 *) (uintptr) s_pOutput->GetBuffer ();
    s_Compositor.nOutputWidth  = s_pOutput->GetWidth ();
    s_Compositor.nOutputHeight = s_pOutput->GetHeight ();
    s_Compositor.nOutputPitch  = s_pOutput->GetPitch ();
    s_Compositor.nOutputBits   = s_pOutput->GetDepth ();
    s_Compositor.pShadow       = s_pShadow;
    s_Compositor.nShadowBytes  = s_nShadowBytes;

    if (!CompositorPlan (&s_Compositor))
    {
        CLogger::Get ()->Write (FROM, LogError, "%ux%u does not fit the %ux%u output",
                                nWidth, nHeight,
                                s_Compositor.nOutputWidth, s_Compositor.nOutputHeight);
        s_bReady = false;
        return false;
    }

    s_Compositor.bFullRedraw = true;
    s_bReady = true;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Mac mode %ux%u %u bpp, shown at %ux scale, origin %u,%u",
                            nWidth, nHeight, nBits,
                            s_Compositor.nScale, s_Compositor.nOriginX,
                            s_Compositor.nOriginY);

    // Onto the card now and not at the next seam: a mode is the moment a
    // screen goes wrong, and a board that stops drawing it takes the log with
    // it. A Macintosh at 912x492 once did exactly that, and left nothing.
    BoardLogFlush ();
    return true;
}

void VideoScreenPalette (const u8 *pRGB, unsigned nEntries)
{
    if (nEntries == 0)
    {
        return;
    }

    const unsigned nRed  = s_bRedLow ? 0 : 16;
    const unsigned nBlue = s_bRedLow ? 16 : 0;
    for (unsigned i = 0; i < 256; i++)
    {
        const unsigned c = i & (nEntries - 1);
        ExpandMap[i] =   0xFF000000
                       | ((u32) pRGB[c * 3 + 0] << nRed)
                       | ((u32) pRGB[c * 3 + 1] << 8)
                       | ((u32) pRGB[c * 3 + 2] << nBlue);
    }

    // Every pixel now maps to a different colour, so what the output shows no
    // longer follows from the guest's bytes: the shadow is stale everywhere.
    s_Compositor.bFullRedraw = true;
}

void VideoScreenInvalidate (void)
{
    s_Compositor.bFullRedraw = true;
}

void VideoScreenAnnounce (int x, int y, int w, int h)
{
    if (s_bReady)
    {
        CompositorAnnounce (&s_Compositor, x, y, w, h);
    }
}

void VideoScreenReadPrefs (void)
{
    const int32 nSkip = PrefsFindInt32 ("frameskip");
    s_bDynamic   = (nSkip <= 0);
    s_nFrameSkip = s_bDynamic ? 1 : (unsigned) nSkip;
}

/*
 *  One vertical blank
 */

// Is the guest drawing at all? A frame buffer that stays blank means the Mac
// never got as far as its first pixel, which looks exactly like a working
// compositor with nothing to show.
static bool GuestHasContent (void)
{
    const u32 nBytes = (u32) s_Compositor.nBytesPerRow * s_Compositor.nHeight;
    for (u32 i = 0; i < nBytes; i += 997)       // sparse probe
    {
        if (s_Compositor.pSource[i] != 0)
        {
            return true;
        }
    }
    return false;
}

// Dynamic: keep compositing to about an eighth of wall time. A VBL is 16667 us,
// so a composite costing C us fits in ceil(C * 8 / 16667) of them. Capped,
// because past a point the screen is a slideshow and the answer is fewer pixels
// — phase 12's dirty regions — and not a slower clock.
static void Retune (unsigned nPerComposite)
{
    if (!s_bDynamic || nPerComposite == 0)
    {
        return;
    }

    unsigned nWanted = (nPerComposite * 8) / 16667 + 1;
    if (nWanted > 12)
    {
        nWanted = 12;
    }
    if (nWanted != s_nFrameSkip)
    {
        // A report like the others: under a drawing load the rate can change
        // at every look, and each line stops the Macintosh while it goes out.
        if (PerfReportWanted ())
        {
            CLogger::Get ()->Write (FROM, LogNotice,
                                    "dynamic: composite %u us, refresh every %u VBL",
                                    nPerComposite, nWanted);
        }
        s_nFrameSkip = nWanted;
    }
}

bool VideoScreenVBL (void)
{
    if (!s_bReady)
    {
        return false;
    }

    // The compositor runs inside the Mac's VBL, so its cost comes straight out
    // of the guest's execution time — and it costs twelve times more once a
    // display is attached, because QEMU then tracks dirty pages on the frame
    // buffer. Hence a rate, and by default a measured one.
    bool bComposited = false;
    if (++s_nVBLsSinceComposite >= s_nFrameSkip)
    {
        s_nVBLsSinceComposite = 0;
        CompositorRun (&s_Compositor);
        bComposited = true;
    }
    s_nVBLs++;

    const unsigned nNow = CTimer::Get ()->GetTicks () / HZ;
    if (nNow == s_nLastReport || (nNow % 5) != 0)
    {
        return bComposited;
    }
    s_nLastReport = nNow;

    // Everything below is measured over the last interval, not since boot.
    // Lifetime averages only ever creep upwards after an expensive mode is
    // visited, which reads as a machine degrading over time, and they made the
    // dynamic rate sluggish to recover.
    const unsigned nPerComposite = s_Compositor.nFrames
                                 ? (unsigned) (s_Compositor.nUsec / s_Compositor.nFrames) : 0;
    Retune (nPerComposite);

    const unsigned nWindow = nNow - s_nLastWindow;
    s_nLastWindow = nNow;

    const unsigned nLoadPerMille = nWindow
                                 ? (unsigned) (s_Compositor.nUsec / nWindow / 1000) : 0;
    const unsigned nBoxesPer = s_Compositor.nFrames
                             ? s_Compositor.nDirtyBoxes / s_Compositor.nFrames : 0;
    const unsigned nScreenRate = nWindow ? s_Compositor.nFrames / nWindow : 0;

    // s_nVBLs counts vertical blanks, not composites. Reporting the VBL rate as
    // "fps" once hid a 9 Hz display behind a reassuring 55.
    if (PerfReportWanted ())
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "%u VBL (%u/s), screen %u/s, composite %u us "
                                "(%u.%u%% of wall), %u/256 boxes, %u full scans, "
                                "guest buffer %s",
                                s_nVBLs, s_nVBLs / (nNow ? nNow : 1),
                                nScreenRate, nPerComposite,
                                nLoadPerMille / 10, nLoadPerMille % 10,
                                nBoxesPer, s_Compositor.nFullScans,
                                GuestHasContent () ? "has content" : "still blank");
    }

    s_Compositor.nUsec       = 0;
    s_Compositor.nFrames     = 0;
    s_Compositor.nDirtyBoxes = 0;
    s_Compositor.nFullScans  = 0;
    return bComposited;
}
