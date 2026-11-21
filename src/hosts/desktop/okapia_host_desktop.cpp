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

#include <SDL3/SDL.h>

#include <errno.h>
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
 *  The log: okapia.log in the folder, as on the card, and the terminal
 */
static FILE *s_pLog;
static bool  s_bQuiet;
static Uint64 s_nStartNs;

void DesktopLogOpen (bool bQuiet)
{
    s_bQuiet = bQuiet;
    s_nStartNs = SDL_GetTicksNS ();
    char Log[1280], Previous[1280];
    snprintf (Log, sizeof Log, "%s/okapia.log", s_Root);
    snprintf (Previous, sizeof Previous, "%s/okapia-previous.log", s_Root);
    rename (Log, Previous);             // nothing to rotate on a first run
    s_pLog = fopen (Log, "w");
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
    snprintf (Path, sizeof Path, "%s/%s", s_Root, bPrevious ? "okapia-previous.log" : "okapia.log");
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
static TSurface      s_Surface;
static bool          s_bHeadless;

bool DesktopWindowOpen (unsigned nWidth, unsigned nHeight, bool bHeadless)
{
    s_bHeadless = bHeadless;
    s_Surface.nWidth  = nWidth;
    s_Surface.nHeight = nHeight;
    s_Surface.nPitch  = nWidth * 4;
    s_Surface.pPixels = (unsigned char *) calloc ((size_t) s_Surface.nPitch * nHeight, 1);
    if (s_Surface.pPixels == 0)
    {
        return false;
    }
    if (bHeadless)
    {
        // No window at all: the surface is the picture, and the test mode
        // writes it out. SDL's offscreen driver would want OpenGL for less.
        HostLog ("desktop", HostLogNotice, "Headless, %ux%u points", nWidth, nHeight);
        return true;
    }
    if (!SDL_CreateWindowAndRenderer ("Okapia", (int) nWidth, (int) nHeight,
                                      SDL_WINDOW_HIGH_PIXEL_DENSITY
                                      | (bHeadless ? SDL_WINDOW_HIDDEN : 0),
                                      &s_pWindow, &s_pRenderer))
    {
        HostLog ("desktop", HostLogError, "No window: %s", SDL_GetError ());
        return false;
    }
    // Points, doubled to pixels by an integer: the Pi's picture at that size.
    SDL_SetRenderLogicalPresentation (s_pRenderer, (int) nWidth, (int) nHeight,
                                      SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    s_pTexture = SDL_CreateTexture (s_pRenderer, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, (int) nWidth, (int) nHeight);
    if (s_pTexture == 0)
    {
        HostLog ("desktop", HostLogError, "No texture: %s", SDL_GetError ());
        return false;
    }
    SDL_SetTextureScaleMode (s_pTexture, SDL_SCALEMODE_NEAREST);
    SDL_StartTextInput (s_pWindow);
    // The menu draws its own pointer, as on the Pi; two would be one too many.
    SDL_HideCursor ();

    int nPixelW = 0, nPixelH = 0;
    SDL_GetWindowSizeInPixels (s_pWindow, &nPixelW, &nPixelH);
    HostLog ("desktop", HostLogNotice, "Window %ux%u points, %dx%d pixels",
             nWidth, nHeight, nPixelW, nPixelH);
    return true;
}

void DesktopWindowClose (void)
{
    if (s_pTexture != 0)  SDL_DestroyTexture (s_pTexture);
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
    return &s_Surface;
}

bool DesktopSavePNG (const char *pPath)
{
    SDL_Surface *pShot = SDL_CreateSurfaceFrom ((int) s_Surface.nWidth, (int) s_Surface.nHeight,
                                                SDL_PIXELFORMAT_XRGB8888, s_Surface.pPixels,
                                                (int) s_Surface.nPitch);
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
 *  and six keys, and relative motion. input_circle.cpp then does the rest
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
static volatile bool s_bMacInput;
static volatile bool s_bDirty;

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

static void Capture (bool bOn)
{
    if (s_pWindow == 0 || bOn == s_bCaptured)
    {
        return;
    }
    s_bCaptured = bOn;
    SDL_SetWindowRelativeMouseMode (s_pWindow, bOn);
    SDL_SetWindowTitle (s_pWindow, bOn ? "Okapia — Control-Command releases the mouse" : "Okapia");
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
            if (!Event.key.repeat)
            {
                HidKey ((unsigned char) Event.key.scancode, Event.type == SDL_EVENT_KEY_DOWN);
            }
            break;
        }
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
        DesktopPostMouse ((int) Event.motion.x, (int) Event.motion.y, 0);
        break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
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
            DesktopPostMouse ((int) Event.button.x, (int) Event.button.y,
                              Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : -1);
        }
        break;

    case SDL_EVENT_WINDOW_EXPOSED:
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
    // The whole surface: the menu's frames are few, and the Macintosh's
    // compositor has already turned its screen into the output (on the Pi the
    // hardware scans this same buffer out).
    SDL_UpdateTexture (s_pTexture, 0, s_Surface.pPixels, (int) s_Surface.nPitch);
    SDL_SetRenderDrawColor (s_pRenderer, 0, 0, 0, 255);
    SDL_RenderClear (s_pRenderer);
    SDL_RenderTexture (s_pRenderer, s_pTexture, 0, 0);
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
        if (s_bMacInput != s_bCaptured && !s_bMacInput)
        {
            Capture (false);
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
    // The host has one output, its default; until the sound arrives with the
    // engine, every entry stands for it.
    return (1u << SoundOff) | (1u << SoundHDMI) | (1u << SoundJack) | (1u << SoundUSB);
}

/*
 *  The network: the host owns it. Until the engine and slirp arrive, off.
 */
unsigned HostNetAvailable (void)            { return 1u << NetworkOff; }
unsigned HostNetRunning (void)              { return NetworkOff; }
bool HostNetTried (void)                    { return false; }
bool HostNetStart (void)                    { return false; }
bool HostNetLinkUp (unsigned *pMbps)        { *pMbps = 0; return false; }
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
