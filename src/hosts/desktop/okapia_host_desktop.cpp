//
// okapia_host_desktop.cpp — okapia_host.h on a desktop: SDL3 for the window,
// the keyboard and the mouse, POSIX for the card folder, the host's own clock.
//
// The boot menu runs on the main thread, which is where SDL wants its events
// pumped on macOS; every wait the menu makes (HostSleep, HostPoll,
// HostInputNext) pumps them, so the window stays alive while the menu thinks.
//
// The menu draws into a surface the size of the window in points, and the GPU
// doubles it to the Retina pixels by nearest neighbour: the picture is the one
// the Pi shows at that size, sharp, and nothing new had to be drawn
// (docs/notes/research/desktop-host.md, "Window, full screen, resolution,
// Retina").
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_host.h"
#include "okapia_card.h"
#include "okapia_keys.h"
#include "okapia_settings.h"
#include "okapia_desktop.h"
#include "install.h"
#include "sync_desktop.h"
#include "board_net_okapia.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#include <copyfile.h>
#endif

/*
 *  The card folder
 */
static char s_Root[1024];

void DesktopSetRoot (const char *pRoot)
{
    snprintf (s_Root, sizeof s_Root, "%s", pRoot);
    // No trailing slash: card paths bring their own.
    size_t n = strlen (s_Root);
    while (n > 1 && s_Root[n - 1] == '/')
    {
        s_Root[--n] = '\0';
    }
}

const char *DesktopRoot (void)
{
    return s_Root;
}

const char *CardPath (const char *pPath)
{
    if (pPath == 0 || pPath[0] != '/' || s_Root[0] == '\0')
    {
        return pPath;
    }
    // Already the host's (a path built from one this returned): as it is.
    const size_t nRoot = strlen (s_Root);
    if (strncmp (pPath, s_Root, nRoot) == 0 && (pPath[nRoot] == '/' || pPath[nRoot] == '\0'))
    {
        return pPath;
    }
    // A ring, because a call site may hold two at once (a rename, a compare).
    static char s_Ring[8][1280];
    static unsigned s_nNext;
    char *pOut = s_Ring[s_nNext++ % 8];
    snprintf (pOut, sizeof s_Ring[0], "%s%s", s_Root, pPath[1] == '\0' ? "" : pPath);
    if (pPath[1] == '\0')
    {
        // "/" itself: the folder.
        snprintf (pOut, sizeof s_Ring[0], "%s", s_Root);
    }
    return pOut;
}

/*
 *  The log: okapia-desktop.log in the folder, and the terminal. Not the
 *  board's okapia.log: a card used here and in a Pi would have the Pi's log
 *  rotated away by every desktop session, and the welcome page reads that log
 *  to say whether the Pi was switched off with a Macintosh running
 */
static FILE *s_pLog;
static bool  s_bQuiet;
static Uint64 s_nStartNs;

static char s_LogRoot[1024];

// The welcome page chose a workspace: the log moves into it, unless it is the
// one the log already is in (named on the command line).
void DesktopLogFollowRoot (void)
{
    if (s_pLog != 0 && strcmp (s_LogRoot, s_Root) == 0)
    {
        return;
    }
    DesktopLogClose ();
    DesktopLogOpen (s_bQuiet);
}

void DesktopLogOpen (bool bQuiet)
{
    s_bQuiet = bQuiet;
    snprintf (s_LogRoot, sizeof s_LogRoot, "%s", s_Root);
    s_nStartNs = SDL_GetTicksNS ();
    if (s_Root[0] == '\0')
    {
        return;                         // the welcome page, no workspace yet: the terminal only
    }
    char Log[1280], Previous[1280];
    snprintf (Log, sizeof Log, "%s/okapia-desktop.log", s_Root);
    snprintf (Previous, sizeof Previous, "%s/okapia-desktop-previous.log", s_Root);
    rename (Log, Previous);             // nothing to rotate on a first run
    s_pLog = fopen (Log, "w");
    DropAppleDouble (Previous);         // on a FAT card, macOS's ._ beside each
    DropAppleDouble (Log);
}

void DesktopLogClose (void)
{
    if (s_pLog != 0)
    {
        fclose (s_pLog);
        s_pLog = 0;
    }
}

void OkapiaHostLogV (const char *pFrom, int nSeverity, const char *pFormat, va_list Args)
{
    char Message[1024];
    vsnprintf (Message, sizeof Message, pFormat, Args);

    time_t nNow = time (0);
    struct tm Local;
    localtime_r (&nNow, &Local);
    char Stamp[32];
    strftime (Stamp, sizeof Stamp, "%b %d %H:%M:%S", &Local);
    const unsigned nCs = (unsigned) ((SDL_GetTicksNS () / 10000000ull) % 100);

    static const char *const Marks[] = { "!", "!", "?", "", "" };
    const char *pMark = nSeverity >= 0 && nSeverity < 5 ? Marks[nSeverity] : "";
    if (s_pLog != 0)
    {
        fprintf (s_pLog, "%s.%02u %s: %s%s\n", Stamp, nCs, pFrom, pMark, Message);
        fflush (s_pLog);
    }
    if (!s_bQuiet || nSeverity <= 2)
    {
        fprintf (stderr, "%s.%02u %s: %s%s\n", Stamp, nCs, pFrom, pMark, Message);
    }
}

void HostLog (const char *pFrom, THostLogLevel Level, const char *pFormat, ...)
{
    // THostLogLevel and TLogSeverity differ by one: Circle's starts at Panic.
    va_list Args;
    va_start (Args, pFormat);
    OkapiaHostLogV (pFrom, (int) Level + 1, pFormat, Args);
    va_end (Args);
}

void HostLogRead (bool bPrevious, THostChunk *pChunk)
{
    if (s_pLog != 0)
    {
        fflush (s_pLog);
    }
    char Path[1280];
    snprintf (Path, sizeof Path, "%s/%s", s_Root, bPrevious ? "okapia-desktop-previous.log" : "okapia-desktop.log");
    FILE *pFile = fopen (Path, "rb");
    if (pFile == 0)
    {
        return;
    }
    static char Chunk[32 * 1024];
    size_t nRead;
    while ((nRead = fread (Chunk, 1, sizeof Chunk, pFile)) > 0)
    {
        pChunk (Chunk, (unsigned) nRead);
    }
    fclose (pFile);
}

/*
 *  The window
 */
static SDL_Window   *s_pWindow;
static SDL_Renderer *s_pRenderer;
static SDL_Texture  *s_pTexture;
// Two outputs. The menu's, s_Surface, in the screen's device pixels, so its
// text is rasterised at the size it is seen; the Macintosh's, s_MacSurface, in
// points, the size Monitors is offered and the compositor draws into (a 3024 by
// 1964 Macintosh is nobody's resolution). Present shows the Macintosh's while
// it runs, the menu's otherwise. The welcome page has only the first.
static SDL_Texture  *s_pMacTexture;
static TSurface      s_Surface;
static TSurface      s_MacSurface;
static unsigned      s_nPointW, s_nPointH;     // the window's output in points
static volatile bool s_bMacInput;              // the Macintosh runs and has the input
static bool          s_bHeadless;

static void SetTitle (void);
static void UpdatePresentation (void);
static void ShowSizeAgain (unsigned nWidth, unsigned nHeight);

static THostWindowMode s_Mode;
static volatile bool s_bDirty;          // the output changed: the main thread presents it

/*
 *  The welcome page's window: no title bar, no frame of the system's — the
 *  page's own frame is the window's edge — sized with the mouse down to a
 *  minimum. The page fills it; a page taller than the window is an output
 *  taller than the window, shown in three bands: its first rows (the title)
 *  and its last (the buttons) where they are, and between them a slice of the
 *  rest under an overlay scroller the host draws and drives (wheel, trackpad,
 *  a drag on the thumb), as the installer's page did. The output's buffer is
 *  allocated once, at the largest size the window may take, in device pixels.
 */
static bool          s_bWelcomeWindow;
static unsigned      s_nCapW, s_nCapH;          // the buffer, in points
static unsigned      s_nViewW, s_nViewH;        // the window, in points
static unsigned      s_nMinW, s_nMinH;          // the smallest it may be, in points
// Device pixels per window point. The welcome page is drawn in the screen's
// own pixels — its outlines rasterised at the size they are seen, not drawn
// in points and doubled — so its output, its bands and the pointer are in
// pixels. The pointer's events arrive in them already (HandleEvent converts
// every event to the renderer's coordinates); the window's size and the hit
// test's areas come in points. 1 for the boot menu, which draws in points.
static float         s_fDensity = 1.0f;
static unsigned      s_nScroll;                 // the output row at the window's top
static bool          s_bResized;
static SDL_Mutex    *s_pOutputLock;             // the output's size and the scroll
static unsigned      s_nBandTop, s_nBandBottom;  // output rows that stay put: the header, the buttons
static unsigned      s_nBandRight;               // the page's frame, right of the scroller
static bool          s_bThumbHeld;
static float         s_fThumbGrip;
static float         s_fPointerX, s_fPointerY;  // in the window

// The scroller: a thumb along the right edge, over the page, as macOS draws
// its overlay scrollers; nothing when the page fits.
static const float SCROLLER_WIDTH = 7.0f, SCROLLER_MARGIN = 3.0f, SCROLLER_REACH = 14.0f;

static void OutputLock (void)
{
    if (s_pOutputLock == 0)
    {
        s_pOutputLock = SDL_CreateMutex ();
    }
    SDL_LockMutex (s_pOutputLock);
}

static void OutputUnlock (void)
{
    SDL_UnlockMutex (s_pOutputLock);
}

// The page does not fit: the middle band scrolls. The bands are taken as
// fixed only while a useful slice is left between them.
static bool Scrolls (void)
{
    return s_bWelcomeWindow && s_Surface.nHeight > s_nViewH
        && s_nViewH > s_nBandTop + s_nBandBottom + 32;
}

static unsigned MaxScroll (void)
{
    return Scrolls () ? s_Surface.nHeight - s_nViewH : 0;
}

// The output row under a window row.
static int OutputY (float fWindowY)
{
    const int nY = (int) fWindowY;
    if (!Scrolls () || nY < (int) s_nBandTop)
    {
        return nY;
    }
    if (nY >= (int) (s_nViewH - s_nBandBottom))
    {
        return nY + (int) (s_Surface.nHeight - s_nViewH);
    }
    return nY + (int) s_nScroll;
}

// The scroller's track, in window rows: the middle band less a margin.
static void Track (float *pfTop, float *pfLength)
{
    *pfTop = (float) s_nBandTop + SCROLLER_MARGIN * s_fDensity;
    *pfLength = (float) (s_nViewH - s_nBandTop - s_nBandBottom) - 2 * SCROLLER_MARGIN * s_fDensity;
}

// The thumb, in window points; false when there is nothing to scroll.
static bool Thumb (SDL_FRect *pThumb)
{
    const unsigned nMax = MaxScroll ();
    if (nMax == 0)
    {
        return false;
    }
    float fTop, fTrack;
    Track (&fTop, &fTrack);
    const float fMiddle = (float) (s_nViewH - s_nBandTop - s_nBandBottom);
    float fHigh = fTrack * fMiddle / (fMiddle + (float) nMax);
    fHigh = fHigh < 24.0f * s_fDensity ? 24.0f * s_fDensity : fHigh;
    pThumb->x = (float) s_nViewW - (float) s_nBandRight - (SCROLLER_MARGIN + SCROLLER_WIDTH) * s_fDensity;
    pThumb->y = fTop + (fTrack - fHigh) * (float) s_nScroll / (float) nMax;
    pThumb->w = SCROLLER_WIDTH * s_fDensity;
    pThumb->h = fHigh;
    return true;
}

static void ScrollTo (float fRow)
{
    const unsigned nMax = MaxScroll ();
    const unsigned nRow = fRow <= 0.0f ? 0 : fRow >= (float) nMax ? nMax : (unsigned) (fRow + 0.5f);
    if (nRow != s_nScroll)
    {
        s_nScroll = nRow;
        s_bDirty = true;
    }
}

static SDL_HitTestResult SDLCALL WelcomeHitTest (SDL_Window *, const SDL_Point *pArea, void *)
{
    // Resizing from the edges is the window's own (it is resizable); the band
    // of the page's title, which never scrolls, moves it as a title bar would.
    if (pArea->y < 6 || (float) pArea->y * s_fDensity < (float) s_nBandTop)
    {
        return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

static THostWindowMode s_MenuMode;
static unsigned        s_nMenuW, s_nMenuH;
static volatile int    s_nWelcomeEnd;   // 1 asked by the engine thread, 2 done by the main thread

void DesktopWindowForWelcome (THostWindowMode Mode, unsigned nWidth, unsigned nHeight)
{
    s_bWelcomeWindow = true;
    s_MenuMode = Mode;
    s_nMenuW = nWidth;
    s_nMenuH = nHeight;
}

void DesktopWindowResize (unsigned nWidth, unsigned nHeight)
{
    if (!s_bWelcomeWindow)
    {
        return;
    }
    if (s_pWindow != 0)
    {
        SDL_SetWindowSize (s_pWindow, (int) nWidth, (int) nHeight);   // its event does the rest
        return;
    }
    OutputLock ();                      // headless: the window it would be, minimum included
    nWidth  = (unsigned) ((float) (nWidth < s_nMinW ? s_nMinW : nWidth) * s_fDensity + 0.5f);
    nHeight = (unsigned) ((float) (nHeight < s_nMinH ? s_nMinH : nHeight) * s_fDensity + 0.5f);
    s_nViewW = nWidth < s_nCapW ? nWidth : s_nCapW;
    s_nViewH = nHeight < s_nCapH ? nHeight : s_nCapH;
    s_bResized = true;
    OutputUnlock ();
}

// What Fill the screen gives: the usable part of the screen (no menu bar, no
// Dock), less the window's own title bar.
static void FillSize (unsigned *pnWidth, unsigned *pnHeight)
{
    if (s_bHeadless)
    {
        *pnWidth = 1920;                // a test's screen, the same every run
        *pnHeight = 1050;
        return;
    }
    SDL_Rect Usable = {0, 0, 1280, 800};
    SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &Usable);
    int nTop = 0, nLeft = 0, nBottom = 0, nRight = 0;
    if (s_pWindow == 0 || !SDL_GetWindowBordersSize (s_pWindow, &nTop, &nLeft, &nBottom, &nRight)
        || nTop <= 0)
    {
        nTop = 28;                      // a macOS title bar, should SDL not say
    }
    *pnWidth  = (unsigned) (Usable.w - nLeft - nRight);
    *pnHeight = (unsigned) (Usable.h - nTop - nBottom);
}

static void ScreenSize (unsigned *pnWidth, unsigned *pnHeight)
{
    *pnWidth = 1920;
    *pnHeight = 1080;
    const SDL_DisplayMode *pMode = s_bHeadless ? 0 : SDL_GetDesktopDisplayMode (SDL_GetPrimaryDisplay ());
    if (pMode != 0)
    {
        *pnWidth  = (unsigned) pMode->w;
        *pnHeight = (unsigned) pMode->h;
    }
}

// The Macintosh's output, in points; and its texture once there is a renderer.
static bool MacSurfaceMake (unsigned nWidth, unsigned nHeight)
{
    free (s_MacSurface.pPixels);
    s_MacSurface.nWidth  = nWidth;
    s_MacSurface.nHeight = nHeight;
    s_MacSurface.nPitch  = nWidth * 4;
    s_MacSurface.pPixels = (unsigned char *) calloc ((size_t) s_MacSurface.nPitch * nHeight, 1);
    return s_MacSurface.pPixels != 0;
}

static bool MacTextureMake (void)
{
    if (s_pMacTexture != 0)
    {
        SDL_DestroyTexture (s_pMacTexture);
    }
    s_pMacTexture = SDL_CreateTexture (s_pRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       (int) s_MacSurface.nWidth, (int) s_MacSurface.nHeight);
    if (s_pMacTexture != 0)
    {
        SDL_SetTextureScaleMode (s_pMacTexture, SDL_SCALEMODE_NEAREST);
    }
    return s_pMacTexture != 0;
}

bool DesktopWindowOpen (unsigned nWidth, unsigned nHeight, bool bHeadless, THostWindowMode Mode)
{
    s_bHeadless = bHeadless;
    s_Mode = Mode;
    unsigned nMinW = 0, nMinH = 0;
    if (s_bWelcomeWindow)
    {
        // The menu's design is 640x480 points; the welcome window is that at
        // the largest of 1, 1.25, 1.5… which leaves a sixth of the screen
        // around it, and may shrink to the page's width and two-thirds of its
        // height (the rest scrolls). The menu keeps that scale whatever size
        // the window then takes.
        SDL_Rect Usable = {0, 0, 1280, 800};
        if (!bHeadless)
        {
            SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &Usable);
        }
        unsigned nScale16 = 16;
        while (nScale16 < 32 && 640 * (nScale16 + 4) / 16 <= (unsigned) Usable.w * 5 / 6
               && 480 * (nScale16 + 4) / 16 <= (unsigned) Usable.h * 5 / 6)
        {
            nScale16 += 4;
        }
        nWidth  = 640 * nScale16 / 16;
        nHeight = 480 * nScale16 / 16;
        nMinW = 600 * nScale16 / 16;
        nMinH = 320 * nScale16 / 16;
        s_nMinW = nMinW;
        s_nMinH = nMinH;
        Mode = s_Mode = HostWindowSized;
        s_nCapW = (unsigned) Usable.w > nWidth ? (unsigned) Usable.w : nWidth;
        s_nCapH = 3 * ((unsigned) Usable.h > nHeight ? (unsigned) Usable.h : nHeight);
        if (!bHeadless)
        {
            // Full screen fills the display, not only its usable part.
            unsigned nScreenW = 0, nScreenH = 0;
            ScreenSize (&nScreenW, &nScreenH);
            s_nCapW = nScreenW > s_nCapW ? nScreenW : s_nCapW;
            s_nCapH = 3 * nScreenH > s_nCapH ? 3 * nScreenH : s_nCapH;
        }
    }
    else
    {
        s_nCapW = nWidth;
        s_nCapH = nHeight;
    }
    s_nViewW = nWidth;
    s_nViewH = nHeight;
    if (!bHeadless && s_bWelcomeWindow)
    {
        if (!SDL_CreateWindowAndRenderer ("Okapia", (int) nWidth, (int) nHeight,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN
                                          | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE,
                                          &s_pWindow, &s_pRenderer))
        {
            HostLog ("desktop", HostLogError, "No window: %s", SDL_GetError ());
            return false;
        }
        SDL_SetWindowMinimumSize (s_pWindow, (int) nMinW, (int) nMinH);
        SDL_SetWindowPosition (s_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        SDL_SetWindowHitTest (s_pWindow, WelcomeHitTest, 0);
    }
    else if (!bHeadless)
    {
        // Fixed in size: the output is the window's for the life of the
        // process, and the Display page changes it (HostWindowApply). Made
        // hidden, so Fill the screen can ask its title bar's height first.
        if (!SDL_CreateWindowAndRenderer ("Okapia", (int) nWidth, (int) nHeight,
                                          SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN,
                                          &s_pWindow, &s_pRenderer))
        {
            HostLog ("desktop", HostLogError, "No window: %s", SDL_GetError ());
            return false;
        }
        if (Mode == HostWindowFillScreen)
        {
            FillSize (&nWidth, &nHeight);
            SDL_SetWindowSize (s_pWindow, (int) nWidth, (int) nHeight);
            SDL_Rect Usable;
            int nTop = 28;
            SDL_GetWindowBordersSize (s_pWindow, &nTop, 0, 0, 0);
            if (SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &Usable))
            {
                SDL_SetWindowPosition (s_pWindow, Usable.x, Usable.y + (nTop > 0 ? nTop : 28));
            }
        }
        else if (Mode == HostWindowFullScreen)
        {
            ScreenSize (&nWidth, &nHeight);
            SDL_SetWindowSize (s_pWindow, (int) nWidth, (int) nHeight);
        }
    }
    {
        // The output in device pixels: the window's points times its density
        // (2 on Retina; a headless run's --density).
        float fDensity = 1.0f;
        if (s_pWindow != 0)
        {
            fDensity = SDL_GetWindowPixelDensity (s_pWindow);
        }
        else if (const char *pTest = SDL_GetHint ("OKAPIA_DENSITY"))
        {
            fDensity = (float) atof (pTest);
        }
        s_fDensity = fDensity >= 1.0f && fDensity <= 4.0f ? fDensity : 1.0f;
        s_nPointW = nWidth;
        s_nPointH = nHeight;
        nWidth  = (unsigned) ((float) nWidth * s_fDensity + 0.5f);
        nHeight = (unsigned) ((float) nHeight * s_fDensity + 0.5f);
        if (s_bWelcomeWindow)
        {
            s_nCapW = (unsigned) ((float) s_nCapW * s_fDensity + 0.5f);
            s_nCapH = (unsigned) ((float) s_nCapH * s_fDensity + 0.5f);
        }
    }
    s_Surface.nWidth  = nWidth;
    s_Surface.nHeight = nHeight;
    s_nViewW = nWidth;
    s_nViewH = nHeight;
    if (!s_bWelcomeWindow)
    {
        s_nCapW = nWidth;
        s_nCapH = nHeight;
    }
    s_Surface.nPitch  = s_nCapW * 4;
    s_Surface.pPixels = (unsigned char *) calloc ((size_t) s_Surface.nPitch * s_nCapH, 1);
    if (s_Surface.pPixels == 0 || (!s_bWelcomeWindow && !MacSurfaceMake (s_nPointW, s_nPointH)))
    {
        return false;
    }
    if (bHeadless)
    {
        // No window at all: the surface is the picture, and the test mode
        // writes it out. SDL's offscreen driver would want OpenGL for less.
        HostLog ("desktop", HostLogNotice, "Headless, %ux%u output pixels, %ux%u points", nWidth, nHeight,
                 s_nPointW, s_nPointH);
        return true;
    }
    // Points, doubled to pixels by an integer: the Pi's picture at that size.
    // The welcome page's output is already the window's pixels: shown as is.
    SDL_SetRenderLogicalPresentation (s_pRenderer, (int) nWidth, (int) nHeight,
                                      s_bWelcomeWindow ? SDL_LOGICAL_PRESENTATION_DISABLED
                                                       : SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    s_pTexture = SDL_CreateTexture (s_pRenderer, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, (int) s_nCapW, (int) s_nCapH);
    if (s_pTexture == 0 || (!s_bWelcomeWindow && !MacTextureMake ()))
    {
        HostLog ("desktop", HostLogError, "No texture: %s", SDL_GetError ());
        return false;
    }
    SDL_SetTextureScaleMode (s_pTexture, SDL_SCALEMODE_NEAREST);
    if (Mode == HostWindowFullScreen)
    {
        SDL_SetWindowFullscreen (s_pWindow, true);
    }
    SDL_ShowWindow (s_pWindow);
    SetTitle ();
    SDL_StartTextInput (s_pWindow);
    // The system's pointer over the menu (HostPointer); the main loop hides
    // it while the Macintosh has the pointer.

    int nPixelW = 0, nPixelH = 0;
    SDL_GetWindowSizeInPixels (s_pWindow, &nPixelW, &nPixelH);
    HostLog ("desktop", HostLogNotice, "Window %ux%u points, output %ux%u pixels, %dx%d on the screen",
             s_nPointW, s_nPointH, nWidth, nHeight, nPixelW, nPixelH);
    return true;
}

// The main thread: the welcome window becomes the boot menu's, with the title
// bar back, and the output its size, as DesktopWindowOpen would have made it.
static void WelcomeWindowToMenu (void)
{
    unsigned nW = s_nMenuW, nH = s_nMenuH;
    s_Mode = s_MenuMode;
    s_bWelcomeWindow = false;
    s_bThumbHeld = false;
    if (s_pWindow != 0)
    {
        SDL_SetWindowHitTest (s_pWindow, 0, 0);
        SDL_SetWindowMinimumSize (s_pWindow, 0, 0);
        SDL_SetWindowResizable (s_pWindow, false);
        SDL_SetWindowBordered (s_pWindow, true);
        if (s_Mode == HostWindowFillScreen)
        {
            FillSize (&nW, &nH);
            SDL_SetWindowSize (s_pWindow, (int) nW, (int) nH);
            SDL_Rect Usable;
            int nTop = 28;
            SDL_GetWindowBordersSize (s_pWindow, &nTop, 0, 0, 0);
            if (SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &Usable))
            {
                SDL_SetWindowPosition (s_pWindow, Usable.x, Usable.y + (nTop > 0 ? nTop : 28));
            }
        }
        else if (s_Mode == HostWindowFullScreen)
        {
            ScreenSize (&nW, &nH);
            SDL_SetWindowSize (s_pWindow, (int) nW, (int) nH);
            SDL_SetWindowFullscreen (s_pWindow, true);
        }
        else
        {
            SDL_SetWindowFullscreen (s_pWindow, false);
            SDL_SetWindowSize (s_pWindow, (int) nW, (int) nH);
            SDL_SetWindowPosition (s_pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    // The same density as the welcome page's: the same window.
    const unsigned nPW = (unsigned) ((float) nW * s_fDensity + 0.5f);
    const unsigned nPH = (unsigned) ((float) nH * s_fDensity + 0.5f);
    unsigned char *pPixels = (unsigned char *) calloc ((size_t) nPW * 4 * nPH, 1);
    if (pPixels == 0 || !MacSurfaceMake (nW, nH))
    {
        HostLog ("desktop", HostLogError, "No output of %ux%u for the boot menu", nW, nH);
        free (pPixels);
        return;                         // the welcome's output, large enough, stays
    }
    OutputLock ();
    free (s_Surface.pPixels);
    s_Surface.pPixels = pPixels;
    s_Surface.nWidth = s_nCapW = s_nViewW = nPW;
    s_Surface.nHeight = s_nCapH = s_nViewH = nPH;
    s_Surface.nPitch = nPW * 4;
    s_nPointW = nW;
    s_nPointH = nH;
    s_nScroll = s_nBandTop = s_nBandBottom = 0;
    s_bResized = false;
    OutputUnlock ();
    if (s_pRenderer != 0)
    {
        SDL_DestroyTexture (s_pTexture);
        s_pTexture = SDL_CreateTexture (s_pRenderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        (int) nPW, (int) nPH);
        if (s_pTexture != 0)
        {
            SDL_SetTextureScaleMode (s_pTexture, SDL_SCALEMODE_NEAREST);
        }
        MacTextureMake ();
        ShowSizeAgain (nPW, nPH);
    }
    SetTitle ();
    s_bDirty = true;
    HostLog ("desktop", HostLogNotice, "Window %ux%u points, the boot menu's, output %ux%u pixels",
             nW, nH, nPW, nPH);
}

void DesktopWelcomeEnd (void)
{
    if (!s_bWelcomeWindow)
    {
        return;
    }
    // SDL's window is the main thread's; the engine thread waits for it, and
    // the output is replaced while nothing draws into it.
    s_nWelcomeEnd = 1;
    while (s_nWelcomeEnd != 2)
    {
        SDL_Delay (5);
    }
    s_nWelcomeEnd = 0;
}

void DesktopWindowClose (void)
{
    if (s_pTexture != 0)  SDL_DestroyTexture (s_pTexture);
    if (s_pMacTexture != 0) SDL_DestroyTexture (s_pMacTexture);
    free (s_MacSurface.pPixels);
    s_MacSurface.pPixels = 0;
    s_pMacTexture = 0;
    if (s_pRenderer != 0) SDL_DestroyRenderer (s_pRenderer);
    if (s_pWindow != 0)   SDL_DestroyWindow (s_pWindow);
    free (s_Surface.pPixels);
    s_pTexture = 0;
    s_pRenderer = 0;
    s_pWindow = 0;
    s_Surface.pPixels = 0;
}

const TSurface *DesktopSurface (void)
{
    return &s_MacSurface;               // the Macintosh's, in points (FwOutputClaim)
}

bool DesktopSavePNG (const char *pPath)
{
    if (Scrolls ())
    {
        // What the welcome window shows: the title's band, the slice, the
        // buttons' band and the thumb, as Present composes them.
        OutputLock ();
        const unsigned nW = s_nViewW, nH = s_nViewH, nOutH = s_Surface.nHeight;
        SDL_Surface *pShot = SDL_CreateSurface ((int) nW, (int) nH, SDL_PIXELFORMAT_XRGB8888);
        if (pShot != 0)
        {
            for (unsigned y = 0; y < nH; y++)
            {
                const unsigned nFrom = y < s_nBandTop ? y : y >= nH - s_nBandBottom ? y + (nOutH - nH)
                                     : y + s_nScroll;
                memcpy ((unsigned char *) pShot->pixels + (size_t) y * pShot->pitch,
                        s_Surface.pPixels + (size_t) nFrom * s_Surface.nPitch, (size_t) nW * 4);
            }
            SDL_FRect Bar;
            if (Thumb (&Bar))
            {
                const SDL_Rect Fill = { (int) Bar.x, (int) Bar.y, (int) Bar.w, (int) Bar.h };
                SDL_FillSurfaceRect (pShot, &Fill, 0x00919191);
            }
        }
        OutputUnlock ();
        if (pShot == 0)
        {
            return false;
        }
        const bool bOK = SDL_SavePNG (pShot, pPath);
        SDL_DestroySurface (pShot);
        HostLog ("desktop", bOK ? HostLogNotice : HostLogError, "Screenshot %s%s", pPath,
                 bOK ? "" : ": could not be written");
        return bOK;
    }
    // What the window shows: the Macintosh's output while it runs.
    const TSurface &Shown = s_bMacInput && s_MacSurface.pPixels != 0 ? s_MacSurface : s_Surface;
    SDL_Surface *pShot = SDL_CreateSurfaceFrom ((int) Shown.nWidth, (int) Shown.nHeight,
                                                SDL_PIXELFORMAT_XRGB8888, Shown.pPixels,
                                                (int) Shown.nPitch);
    if (pShot == 0)
    {
        return false;
    }
    const bool bOK = SDL_SavePNG (pShot, pPath);
    SDL_DestroySurface (pShot);
    HostLog ("desktop", bOK ? HostLogNotice : HostLogError, "Screenshot %s%s", pPath,
             bOK ? "" : ": could not be written");
    return bOK;
}

/*
 *  Input
 *
 *  Two threads, as the note planned: the main thread owns SDL and takes every
 *  event; the emulation thread runs the menu, then the Macintosh. While the menu
 *  is up, events become TEvents in a queue under a lock. While the Macintosh
 *  has the keyboard (InputInit() registered for it, board_desktop.cpp), they
 *  become what the Pi's USB keyboard and mouse send: a HID report of modifiers
 *  and six keys, and relative motion. input_okapia.cpp then does the rest
 *  exactly as on the board, the same table and the same acceleration.
 */
enum { QUEUE = 64 };
static TEvent   s_Queue[QUEUE];
static unsigned s_nHead, s_nTail;
static SDL_Mutex *s_pQueueLock;
static int      s_nX, s_nY;
static unsigned s_nModifiers;
static unsigned s_nSeenModifiers;
static unsigned char s_SeenKeys[32];
static bool     s_bSeenAnything;
static volatile bool s_bQuit;
static unsigned s_nBoundW = 640, s_nBoundH = 480;

// The keyboard as a USB keyboard reports it: the modifier byte and six keys.
static unsigned char s_ucHidModifiers;
static unsigned char s_HidKeys[6];
static unsigned      s_nMouseButtons;
static float         s_fCarryX, s_fCarryY;

static void Lock (void)
{
    if (s_pQueueLock == 0)
    {
        s_pQueueLock = SDL_CreateMutex ();
    }
    SDL_LockMutex (s_pQueueLock);
}

static void Unlock (void)
{
    SDL_UnlockMutex (s_pQueueLock);
}

static void Post (TEventType Type, unsigned nKey, unsigned nChar)
{
    Lock ();
    const unsigned nNext = (s_nHead + 1) % QUEUE;
    if (nNext != s_nTail)               // full: dropped, as on the Pi
    {
        TEvent &e = s_Queue[s_nHead];
        e.Type = Type;
        e.nKey = nKey;
        e.nChar = nChar;
        e.nModifiers = s_nModifiers;
        e.nX = s_nX;
        e.nY = s_nY;
        s_nHead = nNext;
    }
    Unlock ();
}

static unsigned ModifiersOf (SDL_Keymod Mod)
{
    unsigned n = 0;
    if (Mod & SDL_KMOD_CTRL)  n |= ModControl;
    if (Mod & SDL_KMOD_SHIFT) n |= ModShift;
    if (Mod & SDL_KMOD_ALT)   n |= ModOption;
    if (Mod & SDL_KMOD_GUI)   n |= ModCommand;
    return n;
}

void DesktopMacInput (bool bOn)
{
    s_bMacInput = bOn;
    if (!bOn)
    {
        // What was typed at the Macintosh is not an answer to the next window.
        s_nSeenModifiers = 0;
        memset (s_SeenKeys, 0, sizeof s_SeenKeys);
        s_bSeenAnything = false;
    }
}

static void SendHidKeys (void)
{
    DesktopMacKeys (s_ucHidModifiers, s_HidKeys);
}

// A key down or up, by USB usage, for the Macintosh.
static void HidKey (unsigned char ucUsage, bool bDown)
{
    if (ucUsage >= 0xE0 && ucUsage <= 0xE7)
    {
        const unsigned char ucBit = (unsigned char) (1 << (ucUsage - 0xE0));
        s_ucHidModifiers = bDown ? (unsigned char) (s_ucHidModifiers | ucBit)
                                 : (unsigned char) (s_ucHidModifiers & ~ucBit);
        SendHidKeys ();
        return;
    }
    for (unsigned i = 0; i < 6; i++)
    {
        if (s_HidKeys[i] == ucUsage)
        {
            if (bDown)
            {
                return;                 // already down
            }
            s_HidKeys[i] = 0;
            SendHidKeys ();
            return;
        }
    }
    if (!bDown)
    {
        return;
    }
    for (unsigned i = 0; i < 6; i++)
    {
        if (s_HidKeys[i] == 0)
        {
            s_HidKeys[i] = ucUsage;
            SendHidKeys ();
            return;
        }
    }
}

void DesktopPostKey (unsigned char ucUsage, unsigned nModifiers, unsigned nChar)
{
    s_nModifiers = nModifiers;
    s_nSeenModifiers |= nModifiers;
    s_bSeenAnything = true;
    s_SeenKeys[ucUsage >> 3] = (unsigned char) (s_SeenKeys[ucUsage >> 3] | (1 << (ucUsage & 7)));
    const unsigned nKey = KeyOfUsage (ucUsage);
    if (nKey == OkKeySpace && nChar == 0)
    {
        nChar = ' ';
    }
    if (nKey != OkKeyNone || nChar != 0)
    {
        Post (EventKeyDown, nKey, nChar);
    }
}

void DesktopPostText (unsigned nChar, unsigned nModifiers)
{
    s_nModifiers = nModifiers;
    Post (EventKeyDown, OkKeyNone, nChar);
}

// A character a script types, for whichever has the keyboard. The Macintosh
// gets the key that makes it on a US keyboard, with Shift when it needs one.
void DesktopTypeChar (unsigned char c)
{
    if (!s_bMacInput)
    {
        if (c == ' ')
            DesktopPostKey (0x2C, 0, 0);
        else
            DesktopPostText (c, 0);
        return;
    }
    static const char Shifted[] = "!@#$%^&*()";
    unsigned char ucUsage = 0;
    bool bShift = false;
    if (c >= 'a' && c <= 'z')       ucUsage = (unsigned char) (0x04 + c - 'a');
    else if (c >= 'A' && c <= 'Z')  { ucUsage = (unsigned char) (0x04 + c - 'A'); bShift = true; }
    else if (c >= '1' && c <= '9')  ucUsage = (unsigned char) (0x1E + c - '1');
    else if (c == '0')              ucUsage = 0x27;
    else if (c == ' ')              ucUsage = 0x2C;
    else if (c == '.')              ucUsage = 0x37;
    else if (c == ',')              ucUsage = 0x36;
    else if (c == '-')              ucUsage = 0x2D;
    else if (c == '/')              ucUsage = 0x38;
    else
    {
        const char *p = strchr (Shifted, c);
        if (p != 0) { ucUsage = (unsigned char) (0x1E + (p - Shifted)); bShift = true; }
    }
    if (ucUsage == 0)
    {
        return;
    }
    if (bShift) HidKey (0xE1, true);
    HidKey (ucUsage, true);
    HidKey (ucUsage, false);
    if (bShift) HidKey (0xE1, false);
}

// A named key a script presses, for whichever has the keyboard.
void DesktopPressUsage (unsigned char ucUsage, unsigned nModifiers, unsigned nChar)
{
    if (!s_bMacInput)
    {
        DesktopPostKey (ucUsage, nModifiers, nChar);
        return;
    }
    if (nModifiers & ModCommand) HidKey (0xE3, true);
    if (ucUsage == 0 && nChar >= 'a' && nChar <= 'z')
        ucUsage = (unsigned char) (0x04 + nChar - 'a');
    if (ucUsage != 0)
    {
        HidKey (ucUsage, true);
        HidKey (ucUsage, false);
    }
    if (nModifiers & ModCommand) HidKey (0xE3, false);
}

void DesktopPostMouse (int nX, int nY, int nButton)
{
    s_nX = nX < 0 ? 0 : (nX >= (int) s_nBoundW ? (int) s_nBoundW - 1 : nX);
    s_nY = nY < 0 ? 0 : (nY >= (int) s_nBoundH ? (int) s_nBoundH - 1 : nY);
    if (nButton == 0)
    {
        Post (EventMouseMove, OkKeyNone, 0);
    }
    else
    {
        s_bSeenAnything = true;
        Post (nButton > 0 ? EventMouseDown : EventMouseUp, OkKeyNone, 0);
    }
}

void DesktopHoldModifiers (unsigned nModifiers)
{
    s_nModifiers = nModifiers;
    s_nSeenModifiers |= nModifiers;
    if (nModifiers != 0)
    {
        s_bSeenAnything = true;
    }
}

void DesktopRequestQuit (void)
{
    s_bQuit = true;
}

bool DesktopMacHasInput (void)
{
    return s_bMacInput;
}

// One UTF-8 sequence, as a code point; 0 when malformed.
static unsigned NextCodePoint (const char **ppText)
{
    const unsigned char *p = (const unsigned char *) *ppText;
    unsigned c = *p++;
    unsigned nMore = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (nMore != 0)
    {
        c &= 0x3F >> nMore;
        for (; nMore != 0 && (*p & 0xC0) == 0x80; nMore--)
        {
            c = (c << 6) | (*p++ & 0x3F);
        }
        if (nMore != 0)
        {
            c = 0;
        }
    }
    *ppText = (const char *) p;
    return c;
}

// The pointer belongs to the Macintosh while it runs: relative motion, as a
// USB mouse sends, and the host's arrow hidden. Control-Command gives it back.
static bool s_bCaptured;

static char s_Title[256] = "Okapia";

// Full screen hides the host's menu bar. Control-Command, which gives the
// pointer back, brings it back too, so Okapia's menus (File, Quit) are a reach
// away; a click in the window hides it again (menus_desktop.mm).
static bool s_bMenuBarShown;
static bool s_bArrowShown;            // the host's pointer over the window
static volatile bool s_bBeamAsked;    // the menu's pointer is over text (HostPointer)
static bool s_bBeamShown;
static Uint64 s_nMenuBarAgainAt;

static bool IsFullScreen (void)
{
    return s_pWindow != 0 && (SDL_GetWindowFlags (s_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
}

static void *CocoaWindow (void)
{
    return SDL_GetPointerProperty (SDL_GetWindowProperties (s_pWindow),
                                   SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, 0);
}

static void MenuBar (bool bShown)
{
#ifdef __APPLE__
    if (bShown != s_bMenuBarShown && IsFullScreen ())
    {
        s_bMenuBarShown = bShown;
        DesktopMenuBarVisible (CocoaWindow (), bShown);
        // Once more a moment later: SDL hides the bar on its own when the
        // pointer leaves relative mode, after this call.
        s_nMenuBarAgainAt = bShown ? SDL_GetTicks () + 200 : 0;
    }
#else
    (void) bShown;
#endif
}

static void Capture (bool bOn)
{
    if (s_pWindow == 0 || bOn == s_bCaptured)
    {
        return;
    }
    s_bCaptured = bOn;
    SDL_SetWindowRelativeMouseMode (s_pWindow, bOn);
    char Title[320];
    snprintf (Title, sizeof Title, "%s%s", s_Title,
              bOn ? " — Control-Command releases the mouse" : "");
    SDL_SetWindowTitle (s_pWindow, Title);
}

/*
 *  The window's size, and full screen
 *
 *  The surface the menu and the Macintosh draw into is the output, and an
 *  output is claimed once per start, as on the board: its size is the window's
 *  when Okapia started. The window is not resized with the mouse: the Display
 *  page chooses a size, Fill the screen or full screen, and the process starts
 *  again at the menu with that output (HostWindowApply), where the Macintosh is
 *  offered it as a resolution in Monitors (display.md). Control-F still goes
 *  full screen and back at once, showing the same output magnified by the
 *  largest whole factor that fits, nearest neighbour, or shrunk to fit; the
 *  mode is remembered for the next start.
 */
static void Present (void);

// What is magnified: the whole output, or while the Macintosh runs the
// rectangle its screen takes in it (BoardOutputShows). The compositor scales by
// whole output points; the renderer takes that rectangle back to the Mac's own
// size (nearest neighbour undoes a whole factor exactly) and magnifies it by the
// largest whole number of device pixels, so 1008x654 on a 1512x982-point Retina
// screen is three device pixels a Mac pixel and fills it, and 512x384 is five —
// what the Display page's list says (okapia_display.cpp, Rows).
static volatile unsigned s_nMacX, s_nMacY, s_nMacW, s_nMacH, s_nMacScale;
static unsigned s_nShownW, s_nShownH;

void DesktopMacArea (unsigned nX, unsigned nY, unsigned nWidth, unsigned nHeight, unsigned nScale)
{
    s_nMacW = 0;                        // no rectangle while it changes
    s_nMacX = nX;
    s_nMacY = nY;
    s_nMacH = nHeight;
    s_nMacScale = nScale != 0 ? nScale : 1;
    s_nMacW = nWidth;
}

static void ShowSize (unsigned nWidth, unsigned nHeight)
{
    if (nWidth == s_nShownW && nHeight == s_nShownH)
    {
        return;
    }
    s_nShownW = nWidth;
    s_nShownH = nHeight;
    if (s_bWelcomeWindow)
    {
        // Drawn in the window's own pixels: no scaling at all.
        SDL_SetRenderLogicalPresentation (s_pRenderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        return;
    }
    int nW = 0, nH = 0;
    SDL_GetWindowSizeInPixels (s_pWindow, &nW, &nH);
    const bool bFits = nW >= (int) nWidth && nH >= (int) nHeight;
    SDL_SetRenderLogicalPresentation (s_pRenderer, (int) nWidth, (int) nHeight,
                                      bFits ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                            : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetTextureScaleMode (s_pTexture, bFits ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
}

static void ShowSizeAgain (unsigned nWidth, unsigned nHeight)
{
    s_nShownW = s_nShownH = 0;
    ShowSize (nWidth, nHeight);
}

static void UpdatePresentation (void)
{
    if (s_pRenderer == 0)
    {
        return;
    }
    const unsigned nW = s_nShownW != 0 ? s_nShownW : s_Surface.nWidth;
    const unsigned nH = s_nShownH != 0 ? s_nShownH : s_Surface.nHeight;
    s_nShownW = s_nShownH = 0;          // the window changed: measure again
    ShowSize (nW, nH);
}

// Control-F goes full screen and back without a new output; what is
// remembered is the mode, so the next start makes the output fit it.
static void SaveWindowState (void)
{
    if (s_pWindow == 0 || s_bWelcomeWindow)     // the welcome window's size is its own
    {
        return;
    }
    const bool bFull = (SDL_GetWindowFlags (s_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    const THostWindowMode Mode = bFull ? HostWindowFullScreen
                               : s_Mode == HostWindowFullScreen ? HostWindowFillScreen : s_Mode;
    DesktopSaveWindowState (Mode, s_nPointW, s_nPointH);
}

bool HostWindowDescribe (THostWindow *pWindow)
{
    memset (pWindow, 0, sizeof *pWindow);
    pWindow->Mode = s_Mode;
    if (s_pWindow != 0 && (SDL_GetWindowFlags (s_pWindow) & SDL_WINDOW_FULLSCREEN) != 0)
    {
        pWindow->Mode = HostWindowFullScreen;
    }
    pWindow->nWidth  = s_nPointW;            // in points: the Macintosh's output
    pWindow->nHeight = s_nPointH;
    FillSize (&pWindow->nFillWidth, &pWindow->nFillHeight);
    ScreenSize (&pWindow->nScreenWidth, &pWindow->nScreenHeight);
    return true;
}

void HostWindowApply (THostWindowMode Mode, unsigned nWidth, unsigned nHeight, int nTab)
{
    unsigned nW = nWidth, nH = nHeight;
    if (Mode == HostWindowFillScreen)
        FillSize (&nW, &nH);
    else if (Mode == HostWindowFullScreen)
        ScreenSize (&nW, &nH);
    DesktopSaveWindowState (Mode, nW, nH);
    DesktopStartForMenu (Mode, nW, nH, nTab);
}

int HostMenuReopen (void)
{
    // Set by DesktopStartForMenu (main_desktop.cpp) for the process it starts.
    static bool s_bRead;
    if (s_bRead)
    {
        return -1;
    }
    s_bRead = true;
    const char *p = getenv ("OKAPIA_MENU_TAB");
    const int nTab = p != 0 && p[0] >= '0' && p[0] <= '9' ? atoi (p) : -1;
    unsetenv ("OKAPIA_MENU_TAB");
    return nTab;
}

void DesktopToggleFullScreen (void)
{
    if (s_pWindow != 0)
    {
        const bool bFull = (SDL_GetWindowFlags (s_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
        SDL_SetWindowFullscreen (s_pWindow, !bFull);
    }
}

// The window says which Okapia Workspace it shows: its folder's name.
static void SetTitle (void)
{
    const char *pName = strrchr (s_Root, '/');
    if (s_Root[0] == '\0')
    {
        snprintf (s_Title, sizeof s_Title, "Okapia");
    }
    else
    {
        snprintf (s_Title, sizeof s_Title, "Okapia — %s",
                  pName != 0 && pName[1] != '\0' ? pName + 1 : s_Root);
    }
    if (s_pWindow != 0)
    {
        SDL_SetWindowTitle (s_pWindow, s_Title);
    }
}

/*
 *  The Okapia Workspace, from the welcome page (okapia_menu.cpp, FirmwareWelcome)
 */
bool HostHasWelcome (void)
{
    return true;
}

const char *HostLanguage (void)
{
    // The person's first language, before any workspace says one.
    static char Code[3];
    int nCount = 0;
    SDL_Locale **ppLocales = SDL_GetPreferredLocales (&nCount);
    if (ppLocales != 0 && nCount > 0 && ppLocales[0]->language != 0)
    {
        snprintf (Code, sizeof Code, "%s", ppLocales[0]->language);
    }
    SDL_free (ppLocales);
    return Code[0] != '\0' ? Code : 0;
}

// The chooser runs on the main thread, as every host's dialog wants; the
// engine thread, which asked, waits for its answer.
struct TPickJob
{
    THostPick      What;
    const char    *pTitle;
    char          *pOut;
    unsigned       nSize;
    bool           bChosen;
    SDL_Semaphore *pDone;
    const char    *pDefaultName;    // a new workspace's proposed name
};

static void SDLCALL PickAnswered (void *pData, const char *const *ppList, int)
{
    TPickJob *pJob = (TPickJob *) pData;
    pJob->bChosen = ppList != 0 && ppList[0] != 0;
    if (pJob->bChosen)
    {
        snprintf (pJob->pOut, pJob->nSize, "%s", ppList[0]);
    }
    SDL_SignalSemaphore (pJob->pDone);
}

static void SDLCALL PickOnMainThread (void *pData)
{
    TPickJob *pJob = (TPickJob *) pData;
    const bool bFolder = pJob->What == HostPickWorkspace;
    const bool bNew = pJob->What == HostPickWorkspaceNew;
    // SDL opens the system's own chooser: NSOpenPanel on macOS, the common
    // dialog on Windows, the desktop portal or GTK on Linux.
    SDL_PropertiesID Props = SDL_CreateProperties ();
    SDL_SetStringProperty (Props, SDL_PROP_FILE_DIALOG_TITLE_STRING, pJob->pTitle);
    SDL_SetPointerProperty (Props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, s_pWindow);
    char Where[1024];
    if (bNew)
    {
        // A save panel, as for a document: a place and a name, proposed in
        // Documents (or beside the workspace open now). The folder is made by
        // the page once the name is given.
        const char *pHome = getenv ("HOME");
        char Parent[1024];
        snprintf (Parent, sizeof Parent, "%s", s_Root);
        char *pSlash = strrchr (Parent, '/');
        if (Parent[0] != '\0' && pSlash != 0 && pSlash != Parent)
            *pSlash = '\0';
        else
            snprintf (Parent, sizeof Parent, "%s/Documents", pHome != 0 ? pHome : "");
        snprintf (Where, sizeof Where, "%s/%s", Parent, pJob->pDefaultName != 0 ? pJob->pDefaultName : "");
        SDL_SetStringProperty (Props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, Where);
    }
    else if (s_Root[0] != '\0')
    {
        SDL_SetStringProperty (Props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, s_Root);
    }
    SDL_ShowFileDialogWithProperties (bNew ? SDL_FILEDIALOG_SAVEFILE
                                      : bFolder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE,
                                      PickAnswered, pJob, Props);
    SDL_DestroyProperties (Props);
}

bool HostPick (THostPick What, const char *pTitle, char *pOut, unsigned nSize)
{
    if (DesktopScriptPick (pOut, nSize))
    {
        return pOut[0] != '\0';        // the test mode's --pick, in order
    }
    if (SDL_GetHintBoolean ("OKAPIA_HEADLESS", false))
    {
        return false;
    }
    // A new workspace's name is proposed in the page's language: the title's
    // own words would be wrong, and the host has no strings of its own.
    TPickJob Job = { What, pTitle, pOut, nSize, false, SDL_CreateSemaphore (0),
                     What == HostPickWorkspaceNew ? pOut : 0 };
    if (Job.pDone == 0 || !SDL_RunOnMainThread (PickOnMainThread, &Job, false))
    {
        return false;
    }
    SDL_WaitSemaphore (Job.pDone);
    SDL_DestroySemaphore (Job.pDone);
    (void) PickAnswered;
    return Job.bChosen;
}

// A free name in the workspace's root: the file's own, or "name 2.ext".
static bool FreeCardName (const char *pName, char *pCard, unsigned nSize)
{
    const char *pDot = strrchr (pName, '.');
    const int nStem = pDot != 0 && pDot != pName ? (int) (pDot - pName) : (int) strlen (pName);
    const char *pExt = pName + nStem;
    for (unsigned n = 1; n < 100; n++)
    {
        if (n == 1)
            snprintf (pCard, nSize, "/%s", pName);
        else
            snprintf (pCard, nSize, "/%.*s %u%s", nStem, pName, n, pExt);
        struct stat Info;
        if (stat (CardPath (pCard), &Info) != 0)
        {
            return true;
        }
    }
    return false;
}

bool HostCopyToCard (const char *pHostPath, char *pCardPath, unsigned nSize)
{
    char Real[PATH_MAX], Root[PATH_MAX];
    if (realpath (pHostPath, Real) == 0 || realpath (s_Root, Root) == 0)
    {
        return false;
    }
    const char *pName = strrchr (Real, '/') + 1;
    // Already in the workspace: named as it is, not copied again.
    const size_t nRoot = strlen (Root);
    if (strncmp (Real, Root, nRoot) == 0 && Real + nRoot + 1 == pName)
    {
        snprintf (pCardPath, nSize, "/%s", pName);
        return strlen (pCardPath) + 1 < nSize;
    }
    // The card's names stay short (HfsInventory keeps 64 bytes for a path).
    if (strlen (pName) + 8 > nSize || !FreeCardName (pName, pCardPath, nSize))
    {
        HostLog ("desktop", HostLogWarning, "No room for the name %s in the workspace", pName);
        return false;
    }
    // Into a hidden name first, renamed when whole: a stop half-way leaves no
    // half disk image the boot menu would offer.
    char Temp[PATH_MAX + 32], Final[PATH_MAX + 64];
    snprintf (Temp, sizeof Temp, "%s/.okapia-copy", s_Root);
    snprintf (Final, sizeof Final, "%s", CardPath (pCardPath));
    unlink (Temp);
    bool bCopied = false;
#ifdef __APPLE__
    // A clone on APFS: instant, and no space taken until either side changes.
    // CLONE_FORCE, not CLONE: CLONE falls back to a plain copy of its own,
    // which it never syncs, wherever a clone is impossible (a FAT card,
    // another volume); the copy below is the one that syncs.
    if (copyfile (Real, Temp, 0, COPYFILE_CLONE_FORCE | COPYFILE_DATA) == 0)
    {
        const int nFD = open (Temp, O_RDONLY);
        bCopied = nFD >= 0 && HostSyncFile (nFD);
        if (nFD >= 0)
        {
            close (nFD);
        }
    }
#endif
    if (!bCopied)
    {
        unlink (Temp);
        FILE *pIn = fopen (Real, "rb");
        FILE *pOut = pIn != 0 ? fopen (Temp, "wb") : 0;
        bCopied = pOut != 0;
        static char Buffer[1 << 20];
        size_t n;
        while (bCopied && pIn != 0 && (n = fread (Buffer, 1, sizeof Buffer, pIn)) > 0)
        {
            bCopied = fwrite (Buffer, 1, n, pOut) == n;
        }
        bCopied = bCopied && !ferror (pIn) && fflush (pOut) == 0;
        if (pOut != 0)
        {
            bCopied = bCopied && HostSyncFile (fileno (pOut));
            bCopied = fclose (pOut) == 0 && bCopied;
        }
        if (pIn != 0)
        {
            fclose (pIn);
        }
    }
    if (!bCopied || rename (Temp, Final) != 0)
    {
        HostLog ("desktop", HostLogWarning, "Copying %s failed: %s", Real, strerror (errno));
        unlink (Temp);
        return false;
    }
    // The rename is the directory's: synced too, or a stop could leave the
    // image's blocks on the card and no name pointing at them.
    if (!HostSyncDirectory (s_Root))
    {
        // The image is whole and synced; its name may not outlive a power cut.
        HostLog ("desktop", HostLogWarning, "Copied %s to %s, but the folder did not confirm the new name",
                 Real, pCardPath);
        return true;
    }
    HostLog ("desktop", HostLogNotice, "Copied %s to %s", Real, pCardPath);
    return true;
}

static void SDLCALL SetTitleOnMainThread (void *)
{
    SetTitle ();
}

void DesktopRootChanged (void)
{
    SDL_RunOnMainThread (SetTitleOnMainThread, 0, false);
}

const char *HostCardLocation (void)
{
    // The home folder as ~, as the Finder's path bar would say it, so the part
    // that tells one workspace from another is the part that fits.
    static char s_Shown[1024];
    const char *pHome = getenv ("HOME");
    const size_t nHome = pHome != 0 ? strlen (pHome) : 0;
    if (nHome > 1 && strncmp (s_Root, pHome, nHome) == 0 && s_Root[nHome] == '/')
    {
        snprintf (s_Shown, sizeof s_Shown, "~%s", s_Root + nHome);
        return s_Shown;
    }
    return s_Root;
}

static void HandleEvent (SDL_Event &Event)
{
    const bool bMac = s_bMacInput;
    if (!bMac && s_pRenderer != 0)
    {
        SDL_ConvertEventToRenderCoordinates (s_pRenderer, &Event);
    }
    switch (Event.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (!bMac)
        {
            s_bQuit = true;
        }
        else
        {
            DesktopCloseWhileRunning ();
        }
        break;

    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (Event.key.scancode >= 256)
        {
            break;
        }
        if (bMac)
        {
            const SDL_Keymod Mod = Event.key.mod;
            if (Event.type == SDL_EVENT_KEY_DOWN && (Mod & SDL_KMOD_CTRL) && (Mod & SDL_KMOD_GUI)
                && s_bCaptured)
            {
                Capture (false);
            }
        }
        if (Event.type == SDL_EVENT_KEY_DOWN && (Event.key.mod & SDL_KMOD_CTRL)
            && (Event.key.mod & SDL_KMOD_GUI))
        {
            // In the boot menu as well as over the Mac, and after the pointer
            // is given back: leaving relative mode hid the bar again.
            MenuBar (true);
        }
        if (bMac)
        {
            if (!Event.key.repeat)
            {
                HidKey ((unsigned char) Event.key.scancode, Event.type == SDL_EVENT_KEY_DOWN);
            }
            break;
        }
#ifndef __APPLE__
        // Where there is no Cocoa menu (menus_desktop.mm), its shortcuts here:
        // Control-O another workspace, Control-F full screen, Control-Q quit.
        if (Event.type == SDL_EVENT_KEY_DOWN && (Event.key.mod & SDL_KMOD_CTRL) && !Event.key.repeat)
        {
            if (Event.key.scancode == SDL_SCANCODE_F) { DesktopToggleFullScreen (); break; }
            if (Event.key.scancode == SDL_SCANCODE_O) { DesktopChangeWorkspace (); break; }
            if (Event.key.scancode == SDL_SCANCODE_Q) { s_bQuit = true; break; }
        }
#endif
        if (Event.type == SDL_EVENT_KEY_UP)
        {
            s_nModifiers = ModifiersOf (Event.key.mod);
        }
        else if (!Event.key.repeat)
        {
            // A character only for what text input will not send: the space (a
            // key of the menu's as well), and anything typed with Command,
            // which macOS keeps for shortcuts.
            unsigned nChar = 0;
            const unsigned nMods = ModifiersOf (Event.key.mod);
            if (nMods & ModCommand)
            {
                const SDL_Keycode Key = SDL_GetKeyFromScancode (Event.key.scancode,
                                                                SDL_KMOD_NONE, false);
                if (Key >= 0x21 && Key < 0x7F)
                {
                    nChar = (unsigned) Key;
                }
            }
            DesktopPostKey ((unsigned char) Event.key.scancode, nMods, nChar);
        }
        break;

    case SDL_EVENT_TEXT_INPUT:
        if (bMac)
        {
            break;
        }
        for (const char *p = Event.text.text; *p != '\0'; )
        {
            const unsigned c = NextCodePoint (&p);
            // Latin-1, as Circle's keymap gives the Pi; the space comes as a key.
            if (c > 0x20 && c < 0x100 && c != 0x7F)
            {
                DesktopPostText (c, s_nModifiers);
            }
        }
        break;

    case SDL_EVENT_MOUSE_MOTION:
        if (bMac)
        {
            if (s_bCaptured)
            {
                s_fCarryX += Event.motion.xrel;
                s_fCarryY += Event.motion.yrel;
                const int nDX = (int) s_fCarryX, nDY = (int) s_fCarryY;
                s_fCarryX -= (float) nDX;
                s_fCarryY -= (float) nDY;
                if (nDX != 0 || nDY != 0)
                {
                    DesktopMacMouse (s_nMouseButtons, nDX, nDY);
                }
            }
            break;
        }
        // Already in the output's coordinates (HandleEvent converts every
        // event to the renderer's): pixels for the welcome page, points for
        // the boot menu. Only the hit test's areas come in window points.
        s_fPointerX = Event.motion.x;
        s_fPointerY = Event.motion.y;
        if (s_bThumbHeld)
        {
            SDL_FRect Thumb_;
            if (Thumb (&Thumb_))
            {
                float fTop, fTrack;
                Track (&fTop, &fTrack);
                fTrack -= Thumb_.h;
                OutputLock ();
                ScrollTo ((s_fPointerY - s_fThumbGrip - fTop) / (fTrack > 1.0f ? fTrack : 1.0f)
                          * (float) MaxScroll ());
                OutputUnlock ();
            }
            break;
        }
        DesktopPostMouse ((int) s_fPointerX, OutputY (s_fPointerY), 0);
        break;

    case SDL_EVENT_MOUSE_WHEEL:
        if (!bMac && s_bWelcomeWindow)
        {
            // As SDL gives it: already in the direction the system's setting
            // asks (natural scrolling or not), which `direction` only reports.
            const float fRows = Event.wheel.y * (Event.wheel.integer_y != 0 ? 40.0f : 10.0f) * s_fDensity;
            OutputLock ();
            ScrollTo ((float) s_nScroll - fRows);
            OutputUnlock ();
            // The menu's pointer is over another part of the page now.
            DesktopPostMouse ((int) s_fPointerX, OutputY (s_fPointerY), 0);
        }
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            MenuBar (false);            // back to the whole screen
        }
        if (bMac)
        {
            if (!s_bCaptured)
            {
                if (Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
                {
                    Capture (true);     // the click that takes the pointer is not the Mac's
                }
                break;
            }
            const unsigned nBit = Event.button.button == SDL_BUTTON_LEFT ? 1u
                                : Event.button.button == SDL_BUTTON_RIGHT ? 2u : 4u;
            s_nMouseButtons = Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? s_nMouseButtons | nBit
                                                                        : s_nMouseButtons & ~nBit;
            DesktopMacMouse (s_nMouseButtons, 0, 0);
            break;
        }
        if (Event.button.button == SDL_BUTTON_LEFT)
        {
            // In the output's coordinates already (HandleEvent converts them).
            const float fBX = Event.button.x, fBY = Event.button.y;
            SDL_FRect Thumb_;
            if (Event.type == SDL_EVENT_MOUSE_BUTTON_UP && s_bThumbHeld)
            {
                s_bThumbHeld = false;
                break;
            }
            if (Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && Thumb (&Thumb_)
                && fBX >= Thumb_.x + Thumb_.w - SCROLLER_REACH * s_fDensity
                && fBX < (float) (s_nViewW - s_nBandRight)
                && fBY >= (float) s_nBandTop && fBY < (float) (s_nViewH - s_nBandBottom))
            {
                // The scroller's column: the thumb taken where it was held, or
                // the page moved a window's height towards the click.
                if (fBY >= Thumb_.y && fBY < Thumb_.y + Thumb_.h)
                {
                    s_bThumbHeld = true;
                    s_fThumbGrip = fBY - Thumb_.y;
                }
                else
                {
                    OutputLock ();
                    const float fPage = (float) s_nViewH * 0.9f;
                    ScrollTo ((float) s_nScroll + (fBY < Thumb_.y ? -fPage : fPage));
                    OutputUnlock ();
                }
                break;
            }
            DesktopPostMouse ((int) fBX, OutputY (fBY),
                              Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : -1);
        }
        break;

    case SDL_EVENT_WINDOW_EXPOSED:
        s_bDirty = true;
        break;

    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        // Full screen starts with the menu bar hidden; a window keeps its own.
        if (Event.type != SDL_EVENT_WINDOW_RESIZED)
        {
            s_bMenuBarShown = false;
#ifdef __APPLE__
            if (Event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            {
                DesktopMenuBarVisible (CocoaWindow (), true);
            }
#endif
        }
        if (s_bWelcomeWindow)
        {
            int nW = 0, nH = 0;
            SDL_GetWindowSizeInPixels (s_pWindow, &nW, &nH);
            OutputLock ();
            const float fDensity = SDL_GetWindowPixelDensity (s_pWindow);
            if (fDensity >= 1.0f && fDensity != s_fDensity)
            {
                // Another screen: the page keeps its scale (PageFixScale), so
                // it reads larger or smaller there until the next opening.
                HostLog ("desktop", HostLogNotice, "Welcome window now %.2f pixels a point", fDensity);
                s_fDensity = fDensity;
            }
            s_nViewW = nW > 0 ? ((unsigned) nW < s_nCapW ? (unsigned) nW : s_nCapW) : s_nViewW;
            s_nViewH = nH > 0 ? ((unsigned) nH < s_nCapH ? (unsigned) nH : s_nCapH) : s_nViewH;
            s_bResized = true;          // the menu lays the page out again (HostOutputResized)
            OutputUnlock ();
        }
        UpdatePresentation ();
        SaveWindowState ();
        s_bDirty = true;
        break;

    default:
        break;
    }
}

static void Present (void)
{
    if (s_pTexture == 0)
    {
        return;
    }
    if (s_bWelcomeWindow)
    {
        // The title's band, the slice of the steps, the buttons' band; or the
        // whole page when it fits. Then the scroller over the middle.
        OutputLock ();
        const unsigned nW = s_Surface.nWidth < s_nViewW ? s_Surface.nWidth : s_nViewW;
        const unsigned nOutH = s_Surface.nHeight;
        const bool bScrolls = Scrolls ();
        const unsigned nH = bScrolls ? s_nViewH : (nOutH < s_nViewH ? nOutH : s_nViewH);
        const unsigned nTopBand = bScrolls ? s_nBandTop : nH;
        const unsigned nBottomBand = bScrolls ? s_nBandBottom : 0;
        const unsigned nMiddle = nH - nTopBand - nBottomBand;
        const unsigned nScroll = s_nScroll <= MaxScroll () ? s_nScroll : MaxScroll ();
        // Rows of the output and where they go in the window.
        const unsigned From[3] = { 0, nTopBand + nScroll, nOutH - nBottomBand };
        const unsigned To[3]   = { 0, nTopBand, nH - nBottomBand };
        const unsigned High[3] = { nTopBand, nMiddle, nBottomBand };
        for (unsigned i = 0; i < 3; i++)
        {
            if (High[i] != 0)
            {
                const SDL_Rect Slice = { 0, (int) From[i], (int) nW, (int) High[i] };
                SDL_UpdateTexture (s_pTexture, &Slice, s_Surface.pPixels + (size_t) From[i] * s_Surface.nPitch,
                                   (int) s_Surface.nPitch);
            }
        }
        SDL_FRect Bar;
        const bool bBar = Thumb (&Bar);
        OutputUnlock ();
        ShowSize (nW, nH);
        SDL_SetRenderDrawColor (s_pRenderer, 0, 0, 0, 255);
        SDL_RenderClear (s_pRenderer);
        for (unsigned i = 0; i < 3; i++)
        {
            if (High[i] != 0)
            {
                const SDL_FRect Src = { 0.0f, (float) From[i], (float) nW, (float) High[i] };
                const SDL_FRect Dst = { 0.0f, (float) To[i], (float) nW, (float) High[i] };
                SDL_RenderTexture (s_pRenderer, s_pTexture, &Src, &Dst);
            }
        }
        if (bBar)
        {
            SDL_SetRenderDrawBlendMode (s_pRenderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor (s_pRenderer, 0, 0, 0, s_bThumbHeld ? 150 : 110);
            SDL_RenderFillRect (s_pRenderer, &Bar);
        }
        SDL_RenderPresent (s_pRenderer);
        return;
    }
    // The whole surface: the menu's frames are few, and the Macintosh's
    // compositor has already turned its screen into the output (on the Pi the
    // hardware scans this same buffer out).
    // The Macintosh's output while it runs (in points), the menu's otherwise
    // (in device pixels).
    const bool bMac = s_bMacInput && s_pMacTexture != 0;
    const TSurface &Out = bMac ? s_MacSurface : s_Surface;
    SDL_Texture *pTexture = bMac ? s_pMacTexture : s_pTexture;
    SDL_UpdateTexture (pTexture, 0, Out.pPixels, (int) Out.nPitch);
    const unsigned nMacW = s_nMacW, nMacH = s_nMacH, nMacX = s_nMacX, nMacY = s_nMacY;
    const unsigned nScale = s_nMacScale != 0 ? s_nMacScale : 1;
    const bool bMacArea = bMac && nMacW != 0 && nMacH != 0
                       && nMacX + nMacW * nScale <= Out.nWidth
                       && nMacY + nMacH * nScale <= Out.nHeight
                       && (nMacW * nScale < Out.nWidth || nMacH * nScale < Out.nHeight
                           || nScale > 1);
    const SDL_FRect Area = { (float) nMacX, (float) nMacY, (float) (nMacW * nScale),
                             (float) (nMacH * nScale) };
    if (bMacArea)
        ShowSize (nMacW, nMacH);
    else
        ShowSize (Out.nWidth, Out.nHeight);
    SDL_SetRenderDrawColor (s_pRenderer, 0, 0, 0, 255);
    SDL_RenderClear (s_pRenderer);
    SDL_RenderTexture (s_pRenderer, pTexture, bMacArea ? &Area : 0, 0);
    SDL_RenderPresent (s_pRenderer);
}

// The main thread, while the emulation thread runs: events in, pictures out, at
// the display's pace, until pbDone says the other thread has finished.
void DesktopMainLoop (volatile bool *pbDone)
{
    Uint64 nLastFrame = 0;
    while (!*pbDone)
    {
        SDL_Event Event;
        if (SDL_WaitEventTimeout (&Event, 4))
        {
            HandleEvent (Event);
            while (SDL_PollEvent (&Event))
            {
                HandleEvent (Event);
            }
        }
        DesktopScriptStep ();
        DesktopMenuRequests ();
        if (s_nWelcomeEnd == 1)
        {
            WelcomeWindowToMenu ();
            s_nWelcomeEnd = 2;
        }
#ifdef __APPLE__
        if (s_nMenuBarAgainAt != 0 && SDL_GetTicks () >= s_nMenuBarAgainAt)
        {
            s_nMenuBarAgainAt = 0;
            if (s_bMenuBarShown && IsFullScreen ())
            {
                DesktopMenuBarVisible (CocoaWindow (), true);
            }
        }
#endif
        if (s_bMacInput != s_bCaptured && !s_bMacInput)
        {
            Capture (false);
        }
        // The host's pointer: over the menus, which draw none of their own on
        // a desktop (HostPointer), its arrow or text beam; hidden only while
        // the Macintosh has the pointer, and shown as soon as Control-Command
        // gives it back, not only over the menu bar or the Dock.
        const bool bArrow = !(s_bMacInput && s_bCaptured);
        if (s_pWindow != 0 && bArrow != s_bArrowShown)
        {
            s_bArrowShown = bArrow;
            if (bArrow)
                SDL_ShowCursor ();
            else
                SDL_HideCursor ();
        }
        const bool bBeam = s_bBeamAsked && !s_bMacInput;
        if (s_pWindow != 0 && bBeam != s_bBeamShown)
        {
            static SDL_Cursor *s_pBeam, *s_pArrow;
            if (s_pBeam == 0)
            {
                s_pBeam = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_TEXT);
                s_pArrow = SDL_CreateSystemCursor (SDL_SYSTEM_CURSOR_DEFAULT);
            }
            s_bBeamShown = bBeam;
            SDL_SetCursor (bBeam ? s_pBeam : s_pArrow);
        }
        const Uint64 nNow = SDL_GetTicks ();
        if ((s_bDirty || s_bMacInput) && nNow - nLastFrame >= 16)
        {
            s_bDirty = false;
            nLastFrame = nNow;
            Present ();
        }
    }
    Capture (false);
}

void DesktopPump (void)
{
}

bool HostInputWatch (void)
{
    return true;                        // a desktop always has a keyboard
}

void HostInputBounds (unsigned nWidth, unsigned nHeight)
{
    s_nBoundW = nWidth;
    s_nBoundH = nHeight;
}

bool HostInputNext (TEvent *pOut)
{
    Lock ();
    const bool bAny = s_nTail != s_nHead;
    if (bAny)
    {
        *pOut = s_Queue[s_nTail];
        s_nTail = (s_nTail + 1) % QUEUE;
    }
    Unlock ();
    return bAny;
}

void HostInputPointer (int *pX, int *pY)
{
    *pX = s_nX;
    *pY = s_nY;
}

unsigned HostInputSeenModifiers (void)          { return s_nSeenModifiers; }
bool HostInputSeenKey (unsigned char ucUsage)   { return (s_SeenKeys[ucUsage >> 3] >> (ucUsage & 7)) & 1; }
bool HostInputSeenAnything (void)               { return s_bSeenAnything; }
bool HostQuitRequested (void)                   { return s_bQuit; }

/*
 *  The picture
 */
bool HostOutputClaim (TSurface *pSurface)
{
    if (s_Surface.pPixels == 0)
    {
        return false;
    }
    *pSurface = s_Surface;
    return true;
}

void HostOutputPresent (int, int, unsigned, unsigned)
{
    s_bDirty = true;                    // the main thread presents it
}

bool HostPointer (bool bBeam)
{
    s_bBeamAsked = bBeam;               // the main thread sets the shape
    return !s_bHeadless;                // headless: drawn, so a screenshot shows it
}

bool HostOutputResizable (unsigned *pnMaxWidth, unsigned *pnMaxHeight)
{
    if (!s_bWelcomeWindow)
    {
        return false;
    }
    *pnMaxWidth = s_nCapW;
    *pnMaxHeight = s_nCapH;
    return true;
}

bool HostOutputResized (TSurface *pSurface)
{
    if (!s_bWelcomeWindow)
    {
        return false;
    }
    OutputLock ();
    const bool bResized = s_bResized;
    if (bResized)
    {
        s_bResized = false;
        s_Surface.nWidth = s_nViewW;
        s_Surface.nHeight = s_nViewH;
        ScrollTo ((float) s_nScroll);       // within the new range
        *pSurface = s_Surface;
    }
    OutputUnlock ();
    return bResized;
}

void HostOutputExtend (TSurface *pSurface, unsigned nHeight)
{
    if (!s_bWelcomeWindow)
    {
        return;
    }
    OutputLock ();
    if (nHeight == 0)
    {
        s_nScroll = 0;
    }
    nHeight = nHeight < s_nViewH ? s_nViewH : nHeight > s_nCapH ? s_nCapH : nHeight;
    s_Surface.nHeight = nHeight;
    ScrollTo ((float) s_nScroll);
    *pSurface = s_Surface;
    s_bDirty = true;
    OutputUnlock ();
}

void HostOutputBands (unsigned nTop, unsigned nBottom, unsigned nRight)
{
    OutputLock ();
    s_nBandTop = nTop;
    s_nBandBottom = nBottom;
    s_nBandRight = nRight;
    ScrollTo ((float) s_nScroll);
    s_bDirty = true;
    OutputUnlock ();
}

void HostOutputDescribe (THostOutputFacts *pFacts)
{
    memset (pFacts, 0, sizeof *pFacts);
    const SDL_DisplayID Display = s_pWindow != 0 ? SDL_GetDisplayForWindow (s_pWindow)
                                                 : SDL_GetPrimaryDisplay ();
    const SDL_DisplayMode *pMode = SDL_GetDesktopDisplayMode (Display);
    if (pMode != 0)
    {
        // In points, the size a Macintosh desktop would fill (see the note).
        pFacts->nPreferredWidth  = (unsigned) pMode->w;
        pFacts->nPreferredHeight = (unsigned) pMode->h;
    }
    const char *pName = SDL_GetDisplayName (Display);
    snprintf (pFacts->Monitor, sizeof pFacts->Monitor, "%s", pName != 0 ? pName : "");
    const float fDensity = s_pWindow != 0 ? SDL_GetWindowPixelDensity (s_pWindow) : 0.0f;
    pFacts->nDensity = fDensity >= 1.0f ? (unsigned) (fDensity + 0.5f) : 1;
    const char *pTest = SDL_GetHint ("OKAPIA_DENSITY");    // --density, a test's screen
    if (s_pWindow == 0 && pTest != 0 && atoi (pTest) >= 1)
    {
        pFacts->nDensity = (unsigned) atoi (pTest);
    }
}

/*
 *  Time
 */
void HostSleep (unsigned nMs)
{
    SDL_Delay (nMs);
}

void HostPoll (void)
{
    // The main thread takes the events; nothing to drive from this one.
}

unsigned HostLocalTime (void)
{
    time_t nNow = DesktopScriptTime (time (0));
    struct tm Local;
    localtime_r (&nNow, &Local);
    return (unsigned) (nNow + Local.tm_gmtoff);
}

int HostTimeZoneMinutes (void)
{
    time_t nNow = time (0);
    struct tm Local;
    localtime_r (&nNow, &Local);
    return (int) (Local.tm_gmtoff / 60);
}

THostClockSource HostClockSource (const char **ppDetail)
{
    *ppDetail = "";
    return HostClockHost;
}

void HostClockSetZone (const char *pKey, int nFixedMinutes)
{
    // The host keeps the time and knows its zone; the Mac follows it.
    HostLog ("desktop", HostLogDebug, "Zone %s (%d min) kept for the Pi; the host's applies here",
             pKey, nFixedMinutes);
}

void HostClockSetServer (const char *)
{
}

bool HostClockIsHosts (void)
{
    return true;
}

/*
 *  The machine
 */
THostKind HostKind (void)
{
    return HostDesktop;
}

const char *HostMachineName (void)
{
    static char s_Name[96];
    if (s_Name[0] == '\0')
    {
#ifdef __APPLE__
        char Model[48] = "", Version[24] = "";
        size_t nSize = sizeof Model;
        sysctlbyname ("hw.model", Model, &nSize, 0, 0);
        nSize = sizeof Version;
        sysctlbyname ("kern.osproductversion", Version, &nSize, 0, 0);
        snprintf (s_Name, sizeof s_Name, "%s, macOS %s", Model, Version);
#else
        snprintf (s_Name, sizeof s_Name, "%s", SDL_GetPlatform ());
#endif
    }
    return s_Name;
}

void HostMenuPrelude (void)
{
}

bool HostMenuAnswered (void)
{
    // Set by reboot() (main_desktop.cpp) for the process it starts, and read
    // once: a restart from Mac OS later in this process opens the menu again.
    static bool s_bRead;
    if (s_bRead)
    {
        return false;
    }
    s_bRead = true;
    const char *p = getenv ("OKAPIA_MENU_ANSWERED");
    const bool bAnswered = p != 0 && p[0] == '1';
    unsetenv ("OKAPIA_MENU_ANSWERED");
    if (bAnswered)
    {
        HostLog ("desktop", HostLogNotice, "Started again on the menu's answer: no menu this time");
    }
    return bAnswered;
}

/*
 *  The card folder
 */
bool HostCardSpace (unsigned long *pTotalMB, unsigned long *pFreeMB)
{
    struct statfs Info;
    if (statfs (s_Root, &Info) != 0)
    {
        return false;
    }
    *pTotalMB = (unsigned long) ((unsigned long long) Info.f_blocks * Info.f_bsize / (1024 * 1024));
    *pFreeMB  = (unsigned long) ((unsigned long long) Info.f_bavail * Info.f_bsize / (1024 * 1024));
    return true;
}

bool HostFileExists (const char *pPath)
{
    struct stat Info;
    return stat (CardPath (pPath), &Info) == 0;
}

// The whole file allocated before anything is written into it, as on the card:
// a volume that grows on demand would meet a full disk in the middle of a write
// the Macintosh believes has succeeded.
bool HostFileCreate (const char *pPath, unsigned long nMB)
{
    const char *pFull = CardPath (pPath);
    const int nFD = open (pFull, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (nFD < 0)
    {
        return false;
    }
    const off_t nBytes = (off_t) nMB * 1024 * 1024;
    bool bOK;
#ifdef __APPLE__
    fstore_t Store = { F_ALLOCATECONTIG | F_ALLOCATEALL, F_PEOFPOSMODE, 0, nBytes, 0 };
    if (fcntl (nFD, F_PREALLOCATE, &Store) != 0)
    {
        Store.fst_flags = F_ALLOCATEALL;
        bOK = fcntl (nFD, F_PREALLOCATE, &Store) == 0;
    }
    else
    {
        bOK = true;
    }
    bOK = bOK && ftruncate (nFD, nBytes) == 0;
    bOK = bOK && fcntl (nFD, F_FULLFSYNC) == 0;
#else
    bOK = posix_fallocate (nFD, 0, nBytes) == 0 && fsync (nFD) == 0;
#endif
    close (nFD);
    if (!bOK)
    {
        unlink (pFull);
    }
    return bOK;
}

THostRemove HostFileRemove (const char *pPath)
{
    if (unlink (CardPath (pPath)) == 0)
    {
        return HostRemoved;
    }
    return errno == ENOENT ? HostAbsent : HostRemoveFailed;
}

/*
 *  What this host offers
 */
unsigned HostSoundOutputs (void)
{
    // The host's default output, and silence.
    return (1u << SoundOff) | (1u << SoundBuiltIn);
}

/*
 *  The network: the host's, shared with the Macintosh by slirp's NAT
 *  (board_net_desktop.cpp, under the Pi's ether_okapia.cpp). The page's wired
 *  kind is that one (`ether eth`); there is no Wi-Fi to join.
 */
unsigned HostNetAvailable (void)            { return (1u << NetworkOff) | (1u << NetworkCable); }
unsigned HostNetRunning (void)              { return BoardNetKind () == BoardNetWired ? NetworkCable : NetworkOff; }
bool HostNetTried (void)                    { return BoardNetTried (); }
bool HostNetStart (void)                    { return BoardNetStart (); }
bool HostNetLinkUp (unsigned *pMbps)        { return BoardNetLinkUp (pMbps); }
bool HostWlanScan (void)                    { return false; }
unsigned HostWlanSeen (TNetworkSeen *, unsigned) { return 0; }
TNetworkState HostWlanState (char *pSubject, unsigned nSize, int *pDbm, unsigned)
{
    if (nSize > 0)
    {
        pSubject[0] = '\0';
    }
    *pDbm = 0;
    return NetStateUnavailable;
}
bool HostWlanApply (const char *, const char *, const char *) { return false; }
