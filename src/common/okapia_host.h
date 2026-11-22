//
// okapia_host.h — what Okapia asks of the machine it runs on.
//
// Everything above this line is the same on a Raspberry Pi and on a desktop:
// the boot menu, the card's volumes, the preferences. Everything below it is
// one host's: Circle on the bare board (src/hosts/circle/firmware/okapia_host_circle.cpp),
// SDL and POSIX on a desktop (src/hosts/desktop/). The functions are those the
// boot menu needs today; the emulator's own (time, sound, picture) join them as
// they are moved over. See docs/notes/research/desktop-host.md.
//
// No POSIX type crosses it, so that a host without POSIX is a variant and not a
// rewrite. Paths are the card's own, rooted at "/": the host maps them.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_host_h
#define _okapia_host_h

#include "okapia_card_check.h"
#include "okapia_event.h"
#include "okapia_gfx.h"
#include "okapia_network.h"

/*
 *  The log
 */
enum THostLogLevel
{
    HostLogError,
    HostLogWarning,
    HostLogNotice,
    HostLogDebug
};

void HostLog (const char *pFrom, THostLogLevel Level, const char *pFormat, ...)
    __attribute__ ((format (printf, 3, 4)));

// The log as the card holds it, this session's or the previous one's, handed
// over in chunks; nothing when there is none. A host that keeps a ring drains it
// first, so the lines logged a moment ago are there.
typedef void THostChunk (const char *pText, unsigned nLength);
void HostLogRead (bool bPrevious, THostChunk *pChunk);

/*
 *  Time, while the menu waits
 */
// A pause that leaves the host's own work running (the Pi's Wi-Fi tasks).
void HostSleep (unsigned nMs);
// The host's background work between two frames of the menu: the network's
// frames, the clock's questions. Nothing on a host that has threads for them.
void HostPoll (void);

// Local time in seconds since 1970, and the offset in force, in minutes.
unsigned HostLocalTime (void);
int      HostTimeZoneMinutes (void);

enum THostClockSource
{
    HostClockBuild,                     // nothing better than the build's date
    HostClockVolume,                    // the newest date found on a volume
    HostClockMac,                       // set from Mac OS, kept on the card
    HostClockNetwork,                   // a time server answered
    HostClockHost                       // the host's own clock
};
THostClockSource HostClockSource (const char **ppDetail);
// What the Date & Time group set: a zone key, or "" and a fixed offset; and a
// time server, or "" for none. Applied at once, so the page shows the result.
void HostClockSetZone (const char *pKey, int nFixedMinutes);
void HostClockSetServer (const char *pServer);
// Whether this host keeps its own clock (a desktop): the group then shows the
// zone as the host's and offers no time server.
bool HostClockIsHosts (void);

/*
 *  The machine
 */
enum THostKind
{
    HostBoard,                          // the bare Raspberry Pi: a card, an HDMI output
    HostDesktop                         // a window, a card folder, the host's clock
};
THostKind HostKind (void);
const char *HostMachineName (void);
// Once per power-on, before the window: what the host wants done with nothing
// else running (the Pi's network bench, when asked for).
void HostMenuPrelude (void);
// True once when this start follows a start the menu already decided — a
// desktop that started itself again because the PowerPC engine cannot go round
// in place, or because a setting asked for it. The menu then does not open
// again for the same answer, and the startup volume starts. False on a board.
bool HostMenuAnswered (void);

/*
 *  The picture
 */
// The surface the menu draws into, 32 bits a pixel, 0x00RRGGBB. False when
// there is none, and the Macintosh then starts without a menu.
bool HostOutputClaim (TSurface *pSurface);
// The surface changed in this rectangle; a host whose surface is scanned out by
// the hardware does nothing.
void HostOutputPresent (int x, int y, unsigned nWidth, unsigned nHeight);

struct THostOutputFacts
{
    unsigned nPreferredWidth, nPreferredHeight;     // the monitor's, 0 when unknown
    char     Monitor[24];                           // its name, "" when unknown
    bool     bConfigAvailable;                      // the Pi's config.txt exists
    bool     bHdmiDirectives;                       // and sets the mode itself
    // Device pixels per output pixel: 2 on a Retina desktop, whose output is
    // in points; 0 or 1 on a board. The Macintosh's screen is magnified by a
    // whole number of device pixels (desktop.md), which the Display page says.
    unsigned nDensity;
};
void HostOutputDescribe (THostOutputFacts *pFacts);

// A desktop's window, which the Display page sizes. The output is fixed for the
// life of a process (the Macintosh is offered it), so a new size starts the
// process again, at the boot menu, on page nTab.
enum THostWindowMode
{
    HostWindowSized,                    // nWidth × nHeight points
    HostWindowFillScreen,               // as large as the screen allows, title bar kept
    HostWindowFullScreen                // the whole screen
};
struct THostWindow
{
    THostWindowMode Mode;
    unsigned nWidth, nHeight;           // the output now, in points
    unsigned nFillWidth, nFillHeight;   // what Fill the screen would give
    unsigned nScreenWidth, nScreenHeight;
};
bool HostWindowDescribe (THostWindow *pWindow);     // false: a board's output
void HostWindowApply (THostWindowMode Mode, unsigned nWidth, unsigned nHeight, int nTab);
// The welcome page's window, which the person sizes with the mouse: the page
// fills it, and when the page needs more height than the window has, the
// output grows taller than the window and the host shows a slice of it with a
// scroller of its own (pointer coordinates are the output's). A board's output
// never changes: false, and the calls below do nothing.
//   Resizable: the largest output the host can hold.
//   Resized:   true once after the window changed; *pSurface takes the
//              window's size (its height, the shortest the output may be).
//   Extend:    the output's height, at least the window's, at most the
//              largest; 0 asks for exactly the window, scrolled to the top.
//   Bands:     the output's first nTop rows (the page's header, which also
//              moves the window) and last nBottom rows (its buttons) stay in
//              place; only what lies between them scrolls, its scroller kept
//              nRight columns (the page's frame) from the right edge.
// The pointer over the menu, which only a board's menu draws itself: a
// desktop's system shows its own, its arrow or its text beam as asked, and
// answers true; a board answers false and the menu draws one into the output.
bool HostPointer (bool bBeam);

bool HostOutputResizable (unsigned *pnMaxWidth, unsigned *pnMaxHeight);
bool HostOutputResized (TSurface *pSurface);
void HostOutputExtend (TSurface *pSurface, unsigned nHeight);
void HostOutputBands (unsigned nTop, unsigned nBottom, unsigned nRight);
// The page to open the boot menu on when the host started again for it (a new
// window size), at once and without the power-on window; -1 otherwise. Once.
int HostMenuReopen (void);

/*
 *  Input, for the menu
 */
// Starts the watch over the keyboard and mouse; true when a keyboard is there.
bool HostInputWatch (void);
void HostInputBounds (unsigned nWidth, unsigned nHeight);
bool HostInputNext (TEvent *pOut);
void HostInputPointer (int *pX, int *pY);
// What the power-on window saw, latched: modifiers, a key by USB usage, anything.
unsigned HostInputSeenModifiers (void);
bool     HostInputSeenKey (unsigned char ucUsage);
bool     HostInputSeenAnything (void);
// True when the host was asked to quit while the menu was open (a desktop's
// window closed); the menu then answers Shut Down.
bool     HostQuitRequested (void);

/*
 *  The card
 */
// Its size and free space in megabytes; false when unknown.
bool HostCardSpace (unsigned long *pTotalMB, unsigned long *pFreeMB);
// Where the card is, for the Information page: a desktop's Okapia Workspace
// path. 0 on a board, whose card is just the card.
const char *HostCardLocation (void);
bool HostFileExists (const char *pPath);
// A new file of its whole size, allocated before anything is written into it.
bool HostFileCreate (const char *pPath, unsigned long nMB);
enum THostRemove
{
    HostRemoved,
    HostAbsent,
    HostRemoveFailed
};
THostRemove HostFileRemove (const char *pPath);

/*
 *  The Okapia Workspace, on a host that has more than one (a desktop)
 *
 *  The welcome page (FirmwareWelcome) asks for these; a board has its card and
 *  answers false or nothing to all of them.
 */
// Whether the boot menu offers the welcome page, to change workspace.
bool HostHasWelcome (void);
// The language to speak before any workspace says one: "fr", "en", or 0.
const char *HostLanguage (void);
enum THostPick
{
    HostPickWorkspace,                  // an existing folder
    HostPickWorkspaceNew,               // a new one: its place and name, the folder not made yet
    HostPickRom,                        // a file
    HostPickDisk                        // a disk image or a CD image
};
// The host's own chooser, titled. A host path, or false when cancelled.
bool HostPick (THostPick What, const char *pTitle, char *pOut, unsigned nSize);
// The card is now this folder: CardPath() and HostCardLocation() follow.
bool HostWorkspaceSet (const char *pHostPath);
// A file brought into the card, under its own name or a free one: answers the
// card path ("/Quadra 650.rom"). The whole file, as on the card.
bool HostCopyToCard (const char *pHostPath, char *pCardPath, unsigned nSize);
// Leave the welcome page for the workspace's boot menu: the caller goes on to
// start the workspace as any start does, in the same window, and the boot menu
// opens without the power-on window (HostMenuReopen answers its first page).
void HostWorkspaceStart (void);
// From the boot menu: the welcome page, with the workspace in hand.
void HostOpenWelcome (void);
// A ROM's startup chime, read from it as the board plays it (rom_chime):
// whether this reader knows one in the file at that card path, and playing it
// now, in place of anything playing.
bool HostChimeKnown (const char *pCardPath);
bool HostChimeListen (const char *pCardPath);
// The SD cards and other removable volumes mounted now, for the welcome page's
// Carte SD (TCardVolume, okapia_card_check.h); and what any folder's volume
// is. A board has its card and lists nothing.
unsigned HostCards (TCardVolume *pList, unsigned nMax);
bool     HostVolumeOf (const char *pHostPath, TCardVolume *pVolume);
// After the welcome page wrote a file at this card path: what the host left
// beside it goes (macOS keeps a file's extended attributes in "._name" on a
// FAT card, the installer's drop_appledouble).
void     HostTidy (const char *pCardPath);

/*
 *  The release of Okapia a card can be prepared with, and preparing it
 *  (a desktop's: src/hosts/desktop/install.h). Looked for in the background
 *  when the welcome page opens; polled by its Tick.
 */
enum THostReleaseState
{
    HostReleaseLooking,
    HostReleaseReady,                   // its version and boards are known
    HostReleaseNone,                    // the repository has published nothing yet
    HostReleaseFailed                   // offline, or not readable: Why says
};
static const unsigned HOST_BOARDS = 8;
struct THostRelease
{
    THostReleaseState State;
    char     Version[32];               // "26.11.1"
    char     Why[160];
    unsigned nBoards;
    char     BoardId[HOST_BOARDS][16];  // "pi4"
    char     BoardName[HOST_BOARDS][48];// "Raspberry Pi 4"
};
void HostRelease (THostRelease *pOut);

// What the workspace, as a card for this board, holds against the release:
// the version installed ("" none), whether it is that release, and what
// preparing it would write.
struct THostCardPlan
{
    char               Installed[32];
    bool               bUpToDate;
    unsigned long long nBytes;
    bool               bKnown;          // false while there is no release to compare with
    bool               bRefused;        // the release needs a newer Okapia: update the app first
};
void HostCardPlan (const char *pBoardId, THostCardPlan *pOut);

// Puts the release for this board onto the workspace, in the background.
bool HostPrepareStart (const char *pBoardId);
enum THostPrepareState { HostPrepareIdle, HostPrepareFetching, HostPrepareWriting, HostPrepareDone,
                         HostPrepareFailed };
struct THostPrepare
{
    THostPrepareState State;
    char     Name[96];                  // the file being fetched or written
    unsigned nPercent;
    char     Why[160];
};
void HostPrepareStatus (THostPrepare *pOut);

/*
 *  What this host offers
 */
// One bit per TSoundOutput the host has.
unsigned HostSoundOutputs (void);

/*
 *  The network, for its page
 */
// One bit per TNetworkKind this host can have, and the one running now.
unsigned HostNetAvailable (void);
unsigned HostNetRunning (void);
// Whether a start was already tried this power-on: Circle cannot give a device
// back, so a failed start is final until a restart.
bool HostNetTried (void);
bool HostNetStart (void);
bool HostNetLinkUp (unsigned *pMbps);
bool HostWlanScan (void);
unsigned HostWlanSeen (TNetworkSeen *pSeen, unsigned nMax);
// The Wi-Fi's state as the page names it, the network it speaks of, its signal.
TNetworkState HostWlanState (char *pSubject, unsigned nSize, int *pDbm, unsigned nSeen);
bool HostWlanApply (const char *pSSID, const char *pPassword, const char *pCountry);

#endif
