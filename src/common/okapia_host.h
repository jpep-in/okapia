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
};
void HostOutputDescribe (THostOutputFacts *pFacts);

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
