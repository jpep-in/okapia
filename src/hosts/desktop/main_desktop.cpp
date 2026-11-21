//
// main_desktop.cpp — Okapia on a desktop: open a folder laid out like an Okapia
// card, show the boot menu, act on its answer.
//
//   okapia [options] [folder]
//
// The folder, first match wins: the one named, the one holding the
// application (a card folder carried with its program), the last one opened,
// otherwise a picker. Its layout is the card's: BasiliskII_Prefs, the ROMs, the
// disk images, BasiliskII_XPRAM, shared/, okapia.log.
//
// The test mode drives the same binary without a window, on the model of the
// Mamba project's snow-runner (docs/notes/research/desktop-host.md, "The test
// mode"): times are milliseconds since start.
//
//   --headless              no window shown; the picture is still drawn
//   --size WxH              the window, in points (default: 1280x800, or less)
//   --work-dir DIR          copy the folder there first and run on the copy
//   --option                hold Option through the power-on window
//   --keys MS,SEQUENCE      type at MS: letters, digits, {return} {tab} {escape}
//                           {up} {down} {left} {right} {space} {delete} {backspace}
//                           {home} {end} {pageup} {pagedown} {cmd-X}
//   --click X,Y,MS          press and release the button at X,Y (menu points)
//   --screenshot FILE       a PNG of the screen when the run ends
//   --screenshot-at MS      and another at MS, FILE with -MS before .png
//   --datetime "YYYY-MM-DD HH:MM:SS"   the clock, fixed
//   --seconds N             end the run after N seconds (exit code 0)
//   --quiet                 only warnings and errors on the terminal
//
// Exit codes, as snow-runner's where they mean the same: 0 the run ended as
// asked, 1 an error, 3 the menu answered Shut Down.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_desktop.h"
#include "okapia_firmware.h"
#include "okapia_host.h"
#include "okapia_version.h"
#include "okapia_boot.h"

#include "sysdeps.h"
#include "prefs.h"

#include <SDL3/SDL.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define FROM "desktop"


/*
 *  The script
 */
enum TStepKind
{
    StepKeys,
    StepClick,
    StepScreenshot
};

struct TStep
{
    TStepKind Kind;
    Uint64    nAtMs;
    int       nX, nY;
    char      Text[256];
    bool      bDone;
};

static TStep    s_Steps[128];
static unsigned s_nSteps;
static char     s_Screenshot[1024];
static Uint64   s_nDeadlineMs;
static bool     s_bFixedTime;
static time_t   s_nFixedTime;
static Uint64   s_nFixedAtMs;
static Uint64   s_nStartMs;

static TStep *NewStep (TStepKind Kind, Uint64 nAtMs)
{
    if (s_nSteps >= sizeof s_Steps / sizeof s_Steps[0])
    {
        fprintf (stderr, "okapia: too many script steps\n");
        exit (1);
    }
    TStep *p = &s_Steps[s_nSteps++];
    memset (p, 0, sizeof *p);
    p->Kind  = Kind;
    p->nAtMs = nAtMs;
    return p;
}

static void ScreenshotNamed (Uint64 nAtMs)
{
    if (s_Screenshot[0] == '\0')
    {
        return;
    }
    char Path[1100];
    const char *pDot = strrchr (s_Screenshot, '.');
    const int nStem = pDot != 0 ? (int) (pDot - s_Screenshot) : (int) strlen (s_Screenshot);
    snprintf (Path, sizeof Path, "%.*s-%llu%s", nStem, s_Screenshot,
              (unsigned long long) nAtMs, pDot != 0 ? pDot : ".png");
    DesktopSavePNG (Path);
}

static void EndRun (int nCode)
{
    if (s_Screenshot[0] != '\0')
    {
        DesktopSavePNG (s_Screenshot);
    }
    HostLog (FROM, HostLogNotice, "Run ended (code %d)", nCode);
    DesktopLogClose ();
    // Not exit(): the emulation thread is still running the Macintosh, and the
    // destructors exit() runs would pull its world from under it. A test run
    // works on a copy, so this is the plug pulled on purpose, as run-test.sh does.
    _exit (nCode);
}

// One {name} of a key sequence, as the USB usage it stands for.
static unsigned char UsageNamed (const char *pName, size_t nLen)
{
    static const struct { const char *pName; unsigned char ucUsage; } Names[] =
    {
        { "return", 0x28 }, { "enter", 0x58 }, { "escape", 0x29 }, { "tab", 0x2B },
        { "space", 0x2C }, { "backspace", 0x2A }, { "delete", 0x4C },
        { "up", 0x52 }, { "down", 0x51 }, { "left", 0x50 }, { "right", 0x4F },
        { "home", 0x4A }, { "end", 0x4D }, { "pageup", 0x4B }, { "pagedown", 0x4E },
    };
    for (const auto &n : Names)
    {
        if (strlen (n.pName) == nLen && strncmp (n.pName, pName, nLen) == 0)
        {
            return n.ucUsage;
        }
    }
    return 0;
}

static void TypeSequence (const char *p)
{
    while (*p != '\0')
    {
        if (*p == '{')
        {
            const char *pEnd = strchr (p, '}');
            if (pEnd == 0)
            {
                break;
            }
            const size_t nLen = (size_t) (pEnd - p - 1);
            if (nLen == 5 && strncmp (p + 1, "cmd-", 4) == 0)
            {
                // Command and a letter: the shortcut, as the window gets it.
                DesktopPressUsage (0, ModCommand, (unsigned char) p[5]);
            }
            else
            {
                const unsigned char ucUsage = UsageNamed (p + 1, nLen);
                if (ucUsage != 0)
                {
                    DesktopPressUsage (ucUsage, 0, 0);
                }
                else
                {
                    HostLog (FROM, HostLogWarning, "Unknown key {%.*s}", (int) nLen, p + 1);
                }
            }
            p = pEnd + 1;
            continue;
        }
        DesktopTypeChar ((unsigned char) *p++);
    }
}

void DesktopScriptStep (void)
{
    const Uint64 nNow = SDL_GetTicks () - s_nStartMs;
    for (unsigned i = 0; i < s_nSteps; i++)
    {
        TStep *p = &s_Steps[i];
        if (p->bDone || p->nAtMs > nNow)
        {
            continue;
        }
        p->bDone = true;
        switch (p->Kind)
        {
        case StepKeys:
            HostLog (FROM, HostLogNotice, "Script: keys %s", p->Text);
            TypeSequence (p->Text);
            break;

        case StepClick:
            HostLog (FROM, HostLogNotice, "Script: click %d,%d", p->nX, p->nY);
            if (DesktopMacHasInput ())
            {
                HostLog (FROM, HostLogWarning, "Script: clicks reach the menu only, not the Mac yet");
                break;
            }
            DesktopPostMouse (p->nX, p->nY, 0);
            DesktopPostMouse (p->nX, p->nY, 1);
            DesktopPostMouse (p->nX, p->nY, -1);
            break;

        case StepScreenshot:
            ScreenshotNamed (p->nAtMs);
            break;
        }
    }
    if (s_nDeadlineMs != 0 && nNow >= s_nDeadlineMs)
    {
        HostLog (FROM, HostLogNotice, "Deadline reached");
        EndRun (0);
    }
}

time_t DesktopScriptTime (time_t nNow)
{
    if (!s_bFixedTime)
    {
        return nNow;
    }
    // Fixed at the start, and running from there: a clock that stood still would
    // never turn the minute the Date & Time group shows.
    return s_nFixedTime + (time_t) ((SDL_GetTicks () - s_nFixedAtMs) / 1000);
}

/*
 *  The folder
 */
static bool IsCardFolder (const char *pPath)
{
    char Prefs[PATH_MAX + 32];
    struct stat Info;
    if (stat (pPath, &Info) != 0 || !S_ISDIR (Info.st_mode))
    {
        return false;
    }
    snprintf (Prefs, sizeof Prefs, "%s/BasiliskII_Prefs", pPath);
    return stat (Prefs, &Info) == 0;
}

static void LastFolderFile (char *pOut, size_t nSize)
{
    char *pPref = SDL_GetPrefPath ("Okapia", "Okapia");
    snprintf (pOut, nSize, "%slast-folder", pPref != 0 ? pPref : "./");
    SDL_free (pPref);
}

static bool ExecutableFolder (char *pOut, size_t nSize)
{
#ifdef __APPLE__
    char Exe[PATH_MAX];
    uint32_t nExe = sizeof Exe;
    if (_NSGetExecutablePath (Exe, &nExe) != 0)
    {
        return false;
    }
    char Real[PATH_MAX];
    if (realpath (Exe, Real) == 0)
    {
        return false;
    }
    // Inside an application bundle, the folder that holds the bundle.
    char *pApp = strstr (Real, ".app/Contents/MacOS/");
    if (pApp != 0)
    {
        *(pApp + 4) = '\0';
    }
    snprintf (pOut, nSize, "%s", dirname (Real));
    return true;
#else
    (void) pOut;
    (void) nSize;
    return false;
#endif
}

static const char *s_pPicked;
static bool        s_bPickerDone;

static void SDLCALL Picked (void *, const char *const *ppList, int)
{
    s_pPicked = ppList != 0 && ppList[0] != 0 ? strdup (ppList[0]) : 0;
    s_bPickerDone = true;
}

static bool ChooseFolder (const char *pNamed, bool bHeadless, char *pOut, size_t nSize)
{
    char Candidate[PATH_MAX];
    if (pNamed != 0)
    {
        if (realpath (pNamed, pOut) == 0)
        {
            fprintf (stderr, "okapia: %s: %s\n", pNamed, strerror (errno));
            return false;
        }
        return true;
    }
    if (ExecutableFolder (Candidate, sizeof Candidate) && IsCardFolder (Candidate))
    {
        snprintf (pOut, nSize, "%s", Candidate);
        return true;
    }
    char Last[PATH_MAX];
    LastFolderFile (Last, sizeof Last);
    FILE *pFile = fopen (Last, "r");
    if (pFile != 0)
    {
        const bool bRead = fgets (Candidate, sizeof Candidate, pFile) != 0;
        fclose (pFile);
        Candidate[strcspn (Candidate, "\n")] = '\0';
        if (bRead && IsCardFolder (Candidate))
        {
            snprintf (pOut, nSize, "%s", Candidate);
            return true;
        }
    }
    if (bHeadless)
    {
        fprintf (stderr, "okapia: no folder named, and no window to ask in\n");
        return false;
    }
    SDL_ShowOpenFolderDialog (Picked, 0, 0, 0, false);
    while (!s_bPickerDone)
    {
        SDL_Event Event;
        SDL_WaitEventTimeout (&Event, 50);
    }
    if (s_pPicked == 0)
    {
        return false;
    }
    snprintf (pOut, nSize, "%s", s_pPicked);
    return true;
}

static void RememberFolder (const char *pPath)
{
    char Last[PATH_MAX];
    LastFolderFile (Last, sizeof Last);
    FILE *pFile = fopen (Last, "w");
    if (pFile != 0)
    {
        fprintf (pFile, "%s\n", pPath);
        fclose (pFile);
    }
}

// Never two emulators on one card (AGENTS.md): the folder is locked for as
// long as this process runs, and a second one is turned away.
static bool LockFolder (const char *pPath)
{
    const int nFD = open (pPath, O_RDONLY);
    if (nFD < 0 || flock (nFD, LOCK_EX | LOCK_NB) != 0)
    {
        fprintf (stderr, "okapia: %s is in use by another Okapia\n", pPath);
        return false;
    }
    return true;                        // held until the process ends
}

static bool CopyFolder (const char *pFrom, const char *pTo)
{
    // A throwaway copy for a test, as run-test.sh and screenshot.sh use: the
    // card itself is never what a test writes to.
    char Command[3 * PATH_MAX];
    snprintf (Command, sizeof Command, "rm -rf '%s' && mkdir -p '%s' && cp -Rc '%s'/. '%s'/",
              pTo, pTo, pFrom, pTo);
    return system (Command) == 0;
}

/*
 *  The emulation thread
 */
static volatile bool s_bEngineDone;
static int           s_nExitCode;

static int SDLCALL EngineThread (void *)
{
    bool bSwitched = false;
    for (;;)
    {
        const TOkapiaExit Exit = OkapiaRun68k (bSwitched ? 1 : 0);
        bSwitched = false;
        if (Exit == OkapiaHalt)
        {
            HostLog (FROM, HostLogNotice, "Shut down");
            s_nExitCode = 3;
            break;
        }
        if (Exit == OkapiaSwitchToPowerPC)
        {
            // Milestone J3: the PowerPC engine is not in the desktop build yet.
            HostLog (FROM, HostLogWarning, "The startup volume asks for the PowerPC engine, "
                                           "not in this build yet: back to the menu");
            DesktopHoldModifiers (ModOption);
            continue;
        }
        // OkapiaReboot: what a board does by starting again, the process does
        // by going round; the CKernel is kept, as across a restart from Mac OS.
        HostLog (FROM, HostLogNotice, "Restart");
    }
    s_bEngineDone = true;
    return 0;
}

// Closing the window is not pulling the plug (docs/topics/storage.md): while
// the Macintosh runs, it asks first.
void DesktopCloseWhileRunning (void)
{
    static const SDL_MessageBoxButtonData Buttons[] =
    {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel" },
        { 0, 1, "Quit Anyway" },
    };
    const SDL_MessageBoxData Box =
    {
        SDL_MESSAGEBOX_WARNING, 0, "The Macintosh is running",
        "Shut it down from the Special menu first. Quitting now is pulling the plug: the "
        "volumes in use will be repaired at the next start.",
        2, Buttons, 0
    };
    int nButton = 0;
    if (SDL_ShowMessageBox (&Box, &nButton) && nButton == 1)
    {
        HostLog (FROM, HostLogWarning, "Quit while the Macintosh was running");
        DesktopLogClose ();
        _exit (4);
    }
}

/*
 *  The program
 */
static void Usage (void)
{
    fprintf (stderr,
             "usage: okapia [--headless] [--size WxH] [--work-dir DIR] [--option]\n"
             "              [--keys MS,SEQ]... [--click X,Y,MS]... [--screenshot FILE]\n"
             "              [--screenshot-at MS]... [--datetime \"YYYY-MM-DD HH:MM:SS\"]\n"
             "              [--seconds N] [--quiet] [folder]\n");
}

int main (int argc, char **argv)
{
    const char *pFolder = 0, *pWorkDir = 0;
    bool bHeadless = false, bOption = false, bQuiet = false;
    unsigned nWidth = 0, nHeight = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : 0;
        if (strcmp (a, "--headless") == 0)            bHeadless = true;
        else if (strcmp (a, "--option") == 0)         bOption = true;
        else if (strcmp (a, "--quiet") == 0)          bQuiet = true;
        else if (strcmp (a, "--size") == 0 && v)      { sscanf (v, "%ux%u", &nWidth, &nHeight); i++; }
        else if (strcmp (a, "--work-dir") == 0 && v)  { pWorkDir = v; i++; }
        else if (strcmp (a, "--screenshot") == 0 && v){ snprintf (s_Screenshot, sizeof s_Screenshot, "%s", v); i++; }
        else if (strcmp (a, "--screenshot-at") == 0 && v)
        {
            NewStep (StepScreenshot, strtoull (v, 0, 10));
            i++;
        }
        else if (strcmp (a, "--seconds") == 0 && v)   { s_nDeadlineMs = (Uint64) (atof (v) * 1000); i++; }
        else if (strcmp (a, "--keys") == 0 && v)
        {
            const char *pComma = strchr (v, ',');
            if (pComma == 0) { Usage (); return 1; }
            TStep *p = NewStep (StepKeys, strtoull (v, 0, 10));
            snprintf (p->Text, sizeof p->Text, "%s", pComma + 1);
            i++;
        }
        else if (strcmp (a, "--click") == 0 && v)
        {
            int x, y;
            unsigned long long nMs;
            if (sscanf (v, "%d,%d,%llu", &x, &y, &nMs) != 3) { Usage (); return 1; }
            TStep *p = NewStep (StepClick, nMs);
            p->nX = x;
            p->nY = y;
            i++;
        }
        else if (strcmp (a, "--datetime") == 0 && v)
        {
            struct tm Tm;
            memset (&Tm, 0, sizeof Tm);
            if (strptime (v, "%Y-%m-%d %H:%M:%S", &Tm) == 0) { Usage (); return 1; }
            Tm.tm_isdst = -1;
            s_nFixedTime = mktime (&Tm);
            s_bFixedTime = true;
            i++;
        }
        else if (a[0] == '-')                         { Usage (); return 1; }
        else                                          pFolder = a;
    }

    if (bHeadless)
    {
        SDL_SetHint (SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint (SDL_HINT_AUDIO_DRIVER, "dummy");
        // No GPU behind the offscreen driver: the software renderer draws.
        SDL_SetHint (SDL_HINT_RENDER_DRIVER, "software");
    }
    if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fprintf (stderr, "okapia: SDL: %s\n", SDL_GetError ());
        return 1;
    }
    s_nStartMs = SDL_GetTicks ();
    s_nFixedAtMs = s_nStartMs;

    char Folder[PATH_MAX];
    if (!ChooseFolder (pFolder, bHeadless, Folder, sizeof Folder))
    {
        SDL_Quit ();
        return 1;
    }
    if (pWorkDir != 0)
    {
        char Work[PATH_MAX];
        if (!CopyFolder (Folder, pWorkDir) || realpath (pWorkDir, Work) == 0)
        {
            fprintf (stderr, "okapia: cannot copy %s to %s\n", Folder, pWorkDir);
            return 1;
        }
        snprintf (Folder, sizeof Folder, "%s", Work);
    }
    else
    {
        RememberFolder (Folder);
    }
    if (!LockFolder (Folder))
    {
        return 1;
    }

    DesktopSetRoot (Folder);
    DesktopLogOpen (bQuiet);
    HostLog (FROM, HostLogNotice, "Okapia %s (desktop, %s), built " __DATE__ " " __TIME__,
             OKAPIA_VERSION, SDL_GetPlatform ());
    HostLog (FROM, HostLogNotice, "Card folder %s", Folder);

    if (nWidth == 0 || nHeight == 0)
    {
        // The Pi's menu at a size a laptop holds comfortably, in points.
        nWidth = 1280;
        nHeight = 800;
        SDL_Rect Usable;
        if (!bHeadless && SDL_GetDisplayUsableBounds (SDL_GetPrimaryDisplay (), &Usable))
        {
            while (nWidth > 640 && ((int) nWidth > Usable.w * 9 / 10
                                    || (int) nHeight > Usable.h * 9 / 10))
            {
                nWidth  = nWidth * 7 / 8;
                nHeight = nHeight * 7 / 8;
            }
        }
    }
    if (!DesktopWindowOpen (nWidth, nHeight, bHeadless))
    {
        return 1;
    }
    if (bOption)
    {
        DesktopHoldModifiers (ModOption);
    }

    // The emulation thread: the menu, then the Macintosh, as okapia_boot.cpp
    // runs them on the board; this one keeps the window.
    SDL_Thread *pEngine = SDL_CreateThread (EngineThread, "okapia-engine", 0);
    if (pEngine == 0)
    {
        HostLog (FROM, HostLogError, "No emulation thread: %s", SDL_GetError ());
        return 1;
    }
    DesktopMainLoop (&s_bEngineDone);
    SDL_WaitThread (pEngine, 0);

    if (s_Screenshot[0] != '\0')
    {
        DesktopSavePNG (s_Screenshot);
    }
    HostLog (FROM, HostLogNotice, "Okapia ends (code %d)", s_nExitCode);
    DesktopWindowClose ();
    DesktopLogClose ();
    SDL_Quit ();
    return s_nExitCode;
}
