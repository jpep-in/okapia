//
// main_desktop.cpp — Okapia on a desktop: open a folder laid out like an Okapia
// card, show the boot menu, act on its answer.
//
//   okapia [options] [folder]
//
// The folder, first match wins: the one named, the one holding the
// application (a card folder carried with its program), the last one opened,
// otherwise a picker. Its layout is the card's: Okapia_Prefs, the ROMs, the
// disk images, BasiliskII_XPRAM, shared/; the desktop's own log, okapia-desktop.log.
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
//   --resize W,H,MS         the welcome window W x H points at MS, as a mouse would
//   --screenshot FILE       a PNG of the screen when the run ends
//   --screenshot-at MS      and another at MS, FILE with -MS before .png
//   --datetime "YYYY-MM-DD HH:MM:SS"   the clock, fixed
//   --seconds N             end the run after N seconds (exit code 0)
//   --quiet                 only warnings and errors on the terminal
//   --sound-file FILE       the sound written there, raw 44.1 kHz 16-bit stereo
//   --welcome               the welcome page first (also: no workspace yet, or
//                           Shift held at launch), with the folder if one is named
//   --release PATH          the release a card is prepared with: a folder, its
//                           manifest or a board's zip, instead of GitHub's latest
//   --as-release YY.MM.N    this app pretends to be that release (the update notice)
//   --cards-images          a mounted disk image counts as an SD card (tests)
//   --density N             device pixels per point a headless run pretends (2: Retina)
//   --pick PATH             what the welcome page's next chooser answers ("" to
//                           cancel), in order
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
#include <circle/startup.h>

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
    StepResize, StepScreenshot
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
static char     s_Picks[8][1024];
static unsigned s_nPicks, s_nPicked;

bool DesktopScriptPick (char *pOut, unsigned nSize)
{
    if (s_nPicked >= s_nPicks)
    {
        return false;
    }
    snprintf (pOut, nSize, "%s", s_Picks[s_nPicked++]);
    return true;
}

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

        case StepResize:
            HostLog (FROM, HostLogNotice, "Script: window %dx%d", p->nX, p->nY);
            DesktopWindowResize ((unsigned) p->nX, (unsigned) p->nY);
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
    snprintf (Prefs, sizeof Prefs, "%s/Okapia_Prefs", pPath);
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
    // Nothing to open: the welcome page asks, in Okapia's own window.
    (void) bHeadless;
    return false;
}

/*
 *  The window, as it was left: its size in points, or full screen
 */
static void WindowStateFile (char *pOut, size_t nSize)
{
    char *pPref = SDL_GetPrefPath ("Okapia", "Okapia");
    snprintf (pOut, nSize, "%swindow", pPref != 0 ? pPref : "./");
    SDL_free (pPref);
}

void DesktopSaveWindowState (THostWindowMode Mode, unsigned nWidth, unsigned nHeight)
{
    if (SDL_GetHintBoolean ("OKAPIA_HEADLESS", false))
    {
        return;                         // a test's window is no one's
    }
    char Path[PATH_MAX];
    WindowStateFile (Path, sizeof Path);
    FILE *pFile = fopen (Path, "w");
    if (pFile != 0)
    {
        // The mode, and the size it gave: a sized window is opened at it, the
        // other two at what the screen gives then.
        fprintf (pFile, "%s %ux%u\n", Mode == HostWindowFullScreen ? "fullscreen"
                                     : Mode == HostWindowFillScreen ? "fill" : "window", nWidth, nHeight);
        fclose (pFile);
    }
}

static bool LoadWindowState (THostWindowMode *pMode, unsigned *pnWidth, unsigned *pnHeight)
{
    char Path[PATH_MAX];
    WindowStateFile (Path, sizeof Path);
    FILE *pFile = fopen (Path, "r");
    if (pFile == 0)
    {
        return false;
    }
    char Kind[16] = "";
    unsigned nW = 0, nH = 0;
    const bool bOK = fscanf (pFile, "%15s %ux%u", Kind, &nW, &nH) == 3 && nW >= 640 && nH >= 480;
    fclose (pFile);
    if (bOK)
    {
        *pMode = strcmp (Kind, "fullscreen") == 0 ? HostWindowFullScreen
               : strcmp (Kind, "fill") == 0       ? HostWindowFillScreen : HostWindowSized;
        *pnWidth = nW;
        *pnHeight = nH;
    }
    return bOK;
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
static int s_nLockFD = -1;

static bool LockFolder (const char *pPath)
{
    // Closed on exec: reboot() starts the process again and it locks anew.
    const int nFD = open (pPath, O_RDONLY | O_CLOEXEC);
    if (nFD < 0 || flock (nFD, LOCK_EX | LOCK_NB) != 0)
    {
        fprintf (stderr, "okapia: %s is in use by another Okapia\n", pPath);
        if (nFD >= 0)
        {
            close (nFD);
        }
        return false;
    }
    if (s_nLockFD >= 0)
    {
        close (s_nLockFD);              // the workspace the welcome page left
    }
    s_nLockFD = nFD;
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
static bool          s_bWelcome;

// okapia_boot.cpp's loop, as the board runs it: the 68k engine first, and
// whichever one the startup volume asks for after, by a call.
static int SDLCALL EngineThread (void *)
{
    if (s_bWelcome)
    {
        if (!FirmwareWelcome ())
        {
            s_nExitCode = 0;            // Quit
            s_bEngineDone = true;
            return 0;
        }
        // Continue: on with this workspace in the same process and window, as
        // a board goes on from its power-on to its boot menu. Its log in it,
        // and remembered for the next start.
        DesktopLogFollowRoot ();
        if (!SDL_GetHintBoolean ("OKAPIA_HEADLESS", false))
        {
            RememberFolder (DesktopRoot ());
        }
        HostLog (FROM, HostLogNotice, "Okapia Workspace %s, from the welcome page", DesktopRoot ());
    }
    bool bPowerPC = false, bSwitched = false;
    for (;;)
    {
        const TOkapiaExit Exit = bPowerPC ? OkapiaRunPowerPC (bSwitched ? 1 : 0)
                                          : OkapiaRun68k (bSwitched ? 1 : 0);
        bSwitched = false;
        if (Exit == OkapiaHalt)
        {
            HostLog (FROM, HostLogNotice, "Shut down");
            s_nExitCode = 3;
            break;
        }
        if (Exit == OkapiaSwitchToPowerPC || Exit == OkapiaSwitchTo68k)
        {
            bPowerPC  = Exit == OkapiaSwitchToPowerPC;
            bSwitched = true;
            HostLog (FROM, HostLogNotice, "Handing over to the %s engine", bPowerPC ? "PowerPC" : "68k");
            continue;
        }
        // OkapiaReboot: on the board a reset, and here the process started
        // again — what a setting such as the memory needs, the Mac RAM block
        // being claimed once per start.
        reboot ();
    }
    s_bEngineDone = true;
    return 0;
}

// The board's halt() and reboot() (shims/circle/startup.h), which the PowerPC
// engine reaches from QuitEmulator(): it cannot be entered twice in place, so
// on the board a Restart from Mac OS resets the board. A process starts itself
// again, on the same card folder, with no copy and no script.
static char **s_ppArgv;

void halt (void)
{
    HostLog (FROM, HostLogNotice, "Halt");
    DesktopLogClose ();
    _exit (3);
}

void reboot (void)
{
    HostLog (FROM, HostLogNotice, "Reboot: starting again on %s", DesktopRoot ());
    DesktopLogClose ();
    char Exe[PATH_MAX];
    uint32_t nExe = sizeof Exe;
#ifdef __APPLE__
    if (_NSGetExecutablePath (Exe, &nExe) != 0)
#endif
    {
        snprintf (Exe, sizeof Exe, "%s", s_ppArgv[0]);
    }
    const bool bHeadless = SDL_GetHintBoolean ("OKAPIA_HEADLESS", false);
    // The menu has answered already; the new process starts what it chose.
    setenv ("OKAPIA_MENU_ANSWERED", "1", 1);
    char *Args[] = { Exe, (char *) (bHeadless ? "--headless" : "--quiet"),
                     (char *) DesktopRoot (), 0 };
    execv (Exe, Args);
    _exit (1);
}

// Start this application again on a workspace, at its boot menu or at the
// welcome page: a new process, as a new board would be.
static void StartAgain (const char *pFolder, bool bWelcome)
{
    const bool bHeadless = SDL_GetHintBoolean ("OKAPIA_HEADLESS", false);
    HostLog (FROM, HostLogNotice, "Okapia Workspace: %s, starting again there%s",
             pFolder[0] != '\0' ? pFolder : "(none)", bWelcome ? " on the welcome page" : "");
    if (!bHeadless && !bWelcome)
    {
        RememberFolder (pFolder);
    }
    DesktopLogClose ();
    char Exe[PATH_MAX];
    uint32_t nExe = sizeof Exe;
#ifdef __APPLE__
    if (_NSGetExecutablePath (Exe, &nExe) != 0)
#endif
    {
        snprintf (Exe, sizeof Exe, "%s", s_ppArgv[0]);
    }
    unsetenv ("OKAPIA_MENU_ANSWERED");
    char *Args[6];
    unsigned n = 0;
    Args[n++] = Exe;
    Args[n++] = (char *) (bHeadless ? "--headless" : "--quiet");
    if (bWelcome)
    {
        Args[n++] = (char *) "--welcome";
    }
    if (pFolder[0] != '\0')
    {
        Args[n++] = (char *) pFolder;
    }
    Args[n] = 0;
    execv (Exe, Args);
    _exit (1);
}

static bool PullThePlugAnyway (const char *pWhat);

// The Display page chose another window: the output is claimed once per
// process, so the process starts again, straight into the menu at that page.
void DesktopStartForMenu (THostWindowMode Mode, unsigned nWidth, unsigned nHeight, int nTab)
{
    HostLog (FROM, HostLogNotice, "Window changed: starting again at the boot menu");
    DesktopLogClose ();
    char Exe[PATH_MAX];
    uint32_t nExe = sizeof Exe;
#ifdef __APPLE__
    if (_NSGetExecutablePath (Exe, &nExe) != 0)
#endif
    {
        snprintf (Exe, sizeof Exe, "%s", s_ppArgv[0]);
    }
    char Tab[8], Size[32];
    snprintf (Tab, sizeof Tab, "%d", nTab);
    setenv ("OKAPIA_MENU_TAB", Tab, 1);
    unsetenv ("OKAPIA_MENU_ANSWERED");
    // A test's window is remembered nowhere: its size goes on the command line.
    snprintf (Size, sizeof Size, "%ux%u", nWidth, nHeight);
    (void) Mode;
    const bool bHeadless = SDL_GetHintBoolean ("OKAPIA_HEADLESS", false);
    char *Args[] = { Exe, (char *) (bHeadless ? "--headless" : "--quiet"),
                     (char *) (bHeadless ? "--size" : DesktopRoot ()),
                     bHeadless ? Size : 0, bHeadless ? (char *) DesktopRoot () : 0, 0 };
    execv (Exe, Args);
    _exit (1);
}

/*
 *  The welcome page's half of the host (okapia_host.h)
 */
bool HostWorkspaceSet (const char *pHostPath)
{
    char Real[PATH_MAX];
    if (realpath (pHostPath, Real) == 0 || !LockFolder (Real))
    {
        return false;
    }
    DesktopSetRoot (Real);
    DesktopRootChanged ();
    return true;
}

void HostWorkspaceStart (void)
{
    // The boot menu at once, on its first page (HostMenuReopen), in an
    // ordinary window again: the same one, given back its title bar and the
    // boot menu's size.
    setenv ("OKAPIA_MENU_TAB", "0", 1);
    DesktopWelcomeEnd ();
}

void HostOpenWelcome (void)
{
    StartAgain (DesktopRoot (), true);
}

// Change Okapia Workspace (the File menu, Control-O): the welcome page, after
// asking when the Macintosh runs.
void DesktopChangeWorkspace (void)
{
    if (PullThePlugAnyway ("Changing the Okapia Workspace"))
    {
        StartAgain (DesktopRoot (), true);
    }
}

// Asked before anything that stops a running Macintosh from the outside.
static bool PullThePlugAnyway (const char *pWhat)
{
    if (!DesktopMacHasInput ())
    {
        return true;
    }
    static const SDL_MessageBoxButtonData Buttons[] =
    {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "Cancel" },
        { 0, 1, "Continue Anyway" },
    };
    char Text[512];
    snprintf (Text, sizeof Text, "%s while the Macintosh runs is pulling the plug: the volumes in use "
              "will be repaired at the next start. Shut it down from the Special menu first.", pWhat);
    const SDL_MessageBoxData Box =
    {
        SDL_MESSAGEBOX_WARNING, 0, "The Macintosh is running", Text, 2, Buttons, 0
    };
    int nButton = 0;
    return SDL_ShowMessageBox (&Box, &nButton) && nButton == 1;
}

// The File menu's commands (menus_desktop.mm), on the main thread.
void DesktopMenuRequests (void)
{
#ifdef __APPLE__
    const int nRequest = DesktopTakeMenuRequest ();
    switch (nRequest)
    {
    case 1:
        DesktopChangeWorkspace ();
        break;
    case 2:
        DesktopRevealWorkspace (DesktopRoot ());
        break;
    case 3:
        DesktopToggleFullScreen ();
        break;
    case 4:
        DesktopUpdateCheckNow ();
        break;
    case 5:
    case 6:
        DesktopUpdateCheckSet (nRequest == 5);
        break;
    default:
        break;
    }
#endif
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
             "              [--keys MS,SEQ]... [--click X,Y,MS]... [--resize W,H,MS]... [--screenshot FILE]\n"
             "              [--screenshot-at MS]... [--datetime \"YYYY-MM-DD HH:MM:SS\"]\n"
             "              [--seconds N] [--quiet] [--sound-file FILE] [--welcome]\n"
             "              [--pick PATH]... [folder]\n");
}

int main (int argc, char **argv)
{
    s_ppArgv = argv;
    const char *pFolder = 0, *pWorkDir = 0, *pSoundFile = 0;
    bool bHeadless = false, bOption = false, bQuiet = false;
    unsigned nWidth = 0, nHeight = 0;

    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : 0;
        if (strcmp (a, "--headless") == 0)            bHeadless = true;
        else if (strcmp (a, "--option") == 0)         bOption = true;
        else if (strcmp (a, "--quiet") == 0)          bQuiet = true;
        else if (strcmp (a, "--welcome") == 0)        s_bWelcome = true;
        else if (strcmp (a, "--density") == 0 && v)   { SDL_SetHint ("OKAPIA_DENSITY", v); i++; }
        else if (strcmp (a, "--cards-images") == 0)   setenv ("OKAPIA_CARDS_IMAGES", "1", 1);
        else if (strcmp (a, "--release") == 0 && v)   { setenv ("OKAPIA_RELEASE", v, 1); i++; }
        else if (strcmp (a, "--as-release") == 0 && v) { setenv ("OKAPIA_AS_RELEASE", v, 1); i++; }
        else if (strcmp (a, "--pick") == 0 && v)
        {
            if (s_nPicks < sizeof s_Picks / sizeof s_Picks[0])
                snprintf (s_Picks[s_nPicks++], sizeof s_Picks[0], "%s", v);
            i++;
        }
        else if (strcmp (a, "--size") == 0 && v)      { sscanf (v, "%ux%u", &nWidth, &nHeight); i++; }
        else if (strcmp (a, "--work-dir") == 0 && v)  { pWorkDir = v; i++; }
        else if (strcmp (a, "--sound-file") == 0 && v){ pSoundFile = v; i++; }
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
        else if (strcmp (a, "--resize") == 0 && v)
        {
            int w, h;
            unsigned long long nMs;
            if (sscanf (v, "%d,%d,%llu", &w, &h, &nMs) != 3 || w <= 0 || h <= 0) { Usage (); return 1; }
            TStep *p = NewStep (StepResize, nMs);
            p->nX = w;
            p->nY = h;
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
        SDL_SetHint ("OKAPIA_HEADLESS", "1");
        SDL_SetHint (SDL_HINT_VIDEO_DRIVER, "dummy");
        SDL_SetHint (SDL_HINT_AUDIO_DRIVER, pSoundFile != 0 ? "disk" : "dummy");
        // No GPU behind the offscreen driver: the software renderer draws.
        SDL_SetHint (SDL_HINT_RENDER_DRIVER, "software");
    }
    if (pSoundFile != 0)
    {
        // SDL's disk driver: what the Macintosh plays, into a file a test reads.
        SDL_SetHint (SDL_HINT_AUDIO_DRIVER, "disk");
        SDL_SetHint (SDL_HINT_AUDIO_DISK_OUTPUT_FILE, pSoundFile);
    }
    // Full screen over the desktop rather than in a Space of its own: at once,
    // and its menu bar ours to show (Control-Command) and hide (a click).
    SDL_SetHint (SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    if (!SDL_Init (SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        fprintf (stderr, "okapia: SDL: %s\n", SDL_GetError ());
        return 1;
    }
    s_nStartMs = SDL_GetTicks ();
    s_nFixedAtMs = s_nStartMs;

    char Folder[PATH_MAX] = "";
#ifdef __APPLE__
    // Shift held as the application opens: the welcome page, whatever was
    // opened last.
    if (!bHeadless && pFolder == 0 && DesktopShiftHeld ())
    {
        s_bWelcome = true;
    }
#endif
    if (!(s_bWelcome && pFolder == 0) && !ChooseFolder (pFolder, bHeadless, Folder, sizeof Folder))
    {
        if (pFolder != 0)
        {
            SDL_Quit ();
            return 1;
        }
        s_bWelcome = true;              // nothing to open: the welcome page
    }
    if (Folder[0] == '\0')
    {
        // No workspace: the welcome page, its log on the terminal only.
    }
    else if (pWorkDir != 0)
    {
        char Work[PATH_MAX];
        if (!CopyFolder (Folder, pWorkDir) || realpath (pWorkDir, Work) == 0)
        {
            fprintf (stderr, "okapia: cannot copy %s to %s\n", Folder, pWorkDir);
            return 1;
        }
        snprintf (Folder, sizeof Folder, "%s", Work);
    }
    else if (!bHeadless && !s_bWelcome)
    {
        // A window's folder only: a test run must not become the folder the
        // application opens next time (it did, and a user found a test's card).
        RememberFolder (Folder);
    }
    if (Folder[0] != '\0' && !LockFolder (Folder))
    {
        return 1;
    }

    DesktopSetRoot (Folder);
    DesktopLogOpen (bQuiet);
    HostLog (FROM, HostLogNotice, "Okapia %s (desktop, %s), built " __DATE__ " " __TIME__,
             OKAPIA_VERSION, SDL_GetPlatform ());
    HostLog (FROM, HostLogNotice, "Card folder %s%s", Folder[0] != '\0' ? Folder : "(none)",
             s_bWelcome ? ", welcome page" : "");

    THostWindowMode Mode = HostWindowSized;
    if ((nWidth == 0 || nHeight == 0) && !bHeadless)
    {
        // As the Display page left it: a size, or the screen filled, or full
        // screen — the output is that, and the Macintosh is offered it.
        LoadWindowState (&Mode, &nWidth, &nHeight);
    }
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
    if (s_bWelcome)
    {
        // Its own window first; the boot menu's, this one, once it is done.
        DesktopWindowForWelcome (Mode, nWidth, nHeight);
    }
    if (!DesktopWindowOpen (nWidth, nHeight, bHeadless, Mode))
    {
        return 1;
    }
#ifdef __APPLE__
    if (!bHeadless)
    {
        DesktopMenusToControl ();
    }
#endif
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
