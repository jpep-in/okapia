//
// okapia_menu.cpp — the boot menu's driver: the card, the preferences and the
// pages, joined. The same on every host; what it needs of the machine goes
// through okapia_host.h. It was src/firmware/circle/okapia_firmware.cpp until
// the desktop host made the line between the two worth drawing.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_firmware.h"
#include "okapia_host.h"
#include "okapia_card.h"
#include "okapia_keys.h"
#include "okapia_version.h"

#include "net_config_circle.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Basilisk II's headers, and sysdeps.h first as everywhere in that tree. The
// firmware reads exactly one preference and would rather not drag the emulator
// in for it, but declaring PrefsFindString by hand is how a signature drifts.
#include "sysdeps.h"
#include "prefs.h"

#include "hfs_volume_circle.h"

#include "okapia_chooser.h"
#include "okapia_confirm.h"
#include "okapia_datetime.h"
#include "okapia_display.h"
#include "okapia_info.h"
#include "okapia_journal.h"
#include "okapia_network.h"
#include "okapia_newvolume.h"
#include "okapia_page.h"
#include "okapia_settings.h"
#include "okapia_gfx.h"
#include "okapia_screen.h"
#include "okapia_strings.h"
#include "okapia_theme.h"
#include "okapia_clock.h"

#define FROM "firmware"

// Whoever wrote it. Here rather than in the string table because a name is not
// translated, and because the credit is one line that ought to be one line to
// change.
#define OKAPIA_AUTHOR "Jonathan Pepin"


// About two seconds of a still grey field, which is what a Macintosh showed
// before its Happy Mac. Long enough to hold Option, short enough that nobody
// waiting to boot resents it.
static const unsigned WINDOW_MS = 2000;

// The window is spent in slices rather than one sleep, so that whatever
// delivers a USB report — an interrupt, or the cooperative scheduler getting a
// turn — has one. Waiting two seconds in a single call would work only if the
// first were true, and that is exactly the kind of thing not worth assuming.
static const unsigned SLICE_MS = 20;

// USB usage identifiers, which stop at the host for everything except this:
// the zap is asked for by two keys that mean nothing to a menu, so there is no
// logical key to give them and inventing one would be worse (okapia_keys.h).
static const unsigned char KEY_P = UsageP;
static const unsigned char KEY_R = UsageR;

// A pause that lets the host's own work run: on the Pi, the Wi-Fi's tasks of
// Circle's cooperative scheduler, which a plain delay would hold still.
static void FwSleep (unsigned nMs)
{
    HostSleep (nMs);
}

/*
 *  What the card holds, as the chooser wants it
 *
 *  Two sources, joined here and nowhere else: the volumes that exist, from the
 *  inventory, and what the preferences say about them. A volume the preferences
 *  name but the card does not hold is simply gone; a volume the card holds and
 *  the preferences ignore is there, unmounted, waiting to be ticked.
 */
static void GatherVolumes (TChooser *pChooser)
{
    static THfsVolumeInfo Found[CHOOSER_MAX];
    const unsigned nFound = HfsInventory (Found, CHOOSER_MAX);

    pChooser->nCount   = 0;
    pChooser->nStartup = -1;

    for (unsigned i = 0; i < nFound; i++)
    {
        TChooserVolume *v = &pChooser->Volumes[pChooser->nCount];
        unsigned k = 0;
        for (; k + 1 < sizeof v->Path && Found[i].Path[k] != '\0'; k++)
        {
            v->Path[k] = Found[i].Path[k];
        }
        v->Path[k] = '\0';
        for (k = 0; k + 1 < sizeof v->Name && Found[i].Name[k] != '\0'; k++)
        {
            v->Name[k] = Found[i].Name[k];
        }
        v->Name[k] = '\0';

        // Built from the numbers, not from the resource's own short string: a
        // localised System states it as "F1-7.1.2", which is not a version
        // anybody wants to read and whose first character is not the era — and
        // the era is what chooses the icon.
        v->System[0] = '\0';
        v->CPU = CPUUnknown;
        THfsSystemVersion Version;
        if (Found[i].Blessed != 0 && HfsSystemVersion (Found[i].Path, &Version))
        {
            switch (HfsFlavourOf (&Version))
            {
            case HfsFlavour68k:         v->CPU = CPU68k;        break;
            case HfsFlavourPowerPC:     v->CPU = CPUPowerPC;    break;
            case HfsFlavourUniversal:   v->CPU = CPUUniversal;  break;
            default:                    v->CPU = CPUUnknown;    break;
            }

            k = 0;
            v->System[k++] = (char) ('0' + Version.nMajor % 10);
            v->System[k++] = '.';
            v->System[k++] = (char) ('0' + Version.nMinor % 10);
            if (Version.nBugfix != 0)
            {
                v->System[k++] = '.';
                v->System[k++] = (char) ('0' + Version.nBugfix % 10);
            }
            v->System[k] = '\0';
        }
        v->bBootable = Found[i].Blessed != 0;
        v->bClean    = Found[i].bClean;
        v->nFreeKB   = Found[i].FreeKB;
        v->nTotalKB  = Found[i].TotalKB;
        v->bMounted  = false;
        v->Mount     = MountHD;
        // Nothing remembered yet; ChooserSync() settles it from the System, and
        // the preferences below override it where there was a choice to make.
        v->Engine    = CPUUnknown;
        pChooser->nCount++;
    }

    // What the preferences remember about the emulator, before the disks: a
    // volume named by an `engine` line but absent from the card is simply
    // ignored, exactly as a `disk` line for one is.
    for (int nIndex = 0; ; nIndex++)
    {
        const char *pLine = PrefsFindString ("engine", nIndex);
        if (pLine == 0)
        {
            break;
        }
        // "<path> <engine>" — the path first because that is what identifies
        // the volume, and one space because SavePrefsToStream writes one.
        const char *pSpace = pLine;
        while (*pSpace != '\0' && *pSpace != ' ')
        {
            pSpace++;
        }
        if (*pSpace == '\0')
        {
            continue;
        }
        const unsigned nPathLen = (unsigned) (pSpace - pLine);
        const bool bPowerPC = pSpace[1] == 'p';
        for (unsigned i = 0; i < pChooser->nCount; i++)
        {
            const char *a = pChooser->Volumes[i].Path;
            unsigned k = 0;
            while (k < nPathLen && a[k] != '\0' && a[k] == pLine[k])
            {
                k++;
            }
            if (k == nPathLen && a[k] == '\0')
            {
                pChooser->Volumes[i].Engine = bPowerPC ? CPUPowerPC : CPU68k;
                break;
            }
        }
    }

    // Now what the preferences say. The first disk they name is the one the ROM
    // is offered first, so it is the startup volume — that is not a separate
    // setting anywhere, it is the order (disk.cpp:161).
    for (int nIndex = 0; ; nIndex++)
    {
        const char *pDisk = PrefsFindString ("disk", nIndex);
        if (pDisk == 0)
        {
            break;
        }
        const bool bReadOnly = pDisk[0] == '*';
        const char *pPath = bReadOnly ? pDisk + 1 : pDisk;
        for (unsigned i = 0; i < pChooser->nCount; i++)
        {
            const char *a = pChooser->Volumes[i].Path, *b = pPath;
            while (*a != '\0' && *a == *b)
            {
                a++;
                b++;
            }
            if (*a != '\0' || *b != '\0')
            {
                continue;
            }
            pChooser->Volumes[i].bMounted = true;
            pChooser->Volumes[i].Mount    = bReadOnly ? MountHDReadOnly : MountHD;
            if (pChooser->nStartup < 0 && pChooser->Volumes[i].bBootable)
            {
                pChooser->nStartup = (int) i;
            }
            break;
        }
    }

    // And the CD-ROM drives. The startup volume may be one of them, and what
    // says so is `bootdriver`: the ROM is told which driver to start from, not
    // which line comes first (main.cpp:139).
    const bool bBootFromCd = PrefsFindInt32 ("bootdriver") == CHOOSER_BOOT_CDROM;
    for (int nIndex = 0; ; nIndex++)
    {
        const char *pCd = PrefsFindString ("cdrom", nIndex);
        if (pCd == 0)
        {
            break;
        }
        for (unsigned i = 0; i < pChooser->nCount; i++)
        {
            const char *a = pChooser->Volumes[i].Path, *b = pCd;
            while (*a != '\0' && *a == *b)
            {
                a++;
                b++;
            }
            if (*a != '\0' || *b != '\0')
            {
                continue;
            }
            pChooser->Volumes[i].bMounted = true;
            pChooser->Volumes[i].Mount    = MountCD;
            // The first disc listed, when the machine is set to start from one:
            // that is the drive the CD driver offers first, which is the only
            // thing that distinguishes two discs to a ROM told only the driver.
            if (bBootFromCd && nIndex == 0 && pChooser->Volumes[i].bBootable)
            {
                pChooser->nStartup = (int) i;
            }
            break;
        }
    }
}

// What the user chose, onto the card. Every `disk` line is replaced rather than
// edited: the order is the setting, so there is nothing to edit in place.
static void ApplyVolumes (const TChooser *pChooser)
{
    static char Lines[CHOOSER_MAX][CHOOSER_LINE];
    const unsigned n = ChooserDiskLines (pChooser, Lines, CHOOSER_MAX);

    while (PrefsFindString ("disk", 0) != 0)
    {
        PrefsRemoveItem ("disk", 0);
    }
    for (unsigned i = 0; i < n; i++)
    {
        PrefsAddString ("disk", Lines[i]);
        HostLog (FROM, HostLogNotice, "disk %s", Lines[i]);
    }

    // Which driver the Macintosh starts from. Written every time, including the
    // 0 that means "no preference": a card that once started from a disc would
    // otherwise go on asking for the CD driver after the disc was gone.
    const int nBootDriver = ChooserBootDriver (pChooser);
    PrefsReplaceInt32 ("bootdriver", nBootDriver);
    HostLog (FROM, HostLogNotice, "bootdriver %d", nBootDriver);

    // The same for the CD-ROM drives, and it has to be the same call: a volume
    // moved from one list to the other has to leave the first, and rewriting
    // only the list it arrived in would mount it twice.
    const unsigned nCds = ChooserCdromLines (pChooser, Lines, CHOOSER_MAX);
    while (PrefsFindString ("cdrom", 0) != 0)
    {
        PrefsRemoveItem ("cdrom", 0);
    }
    for (unsigned i = 0; i < nCds; i++)
    {
        PrefsAddString ("cdrom", Lines[i]);
        HostLog (FROM, HostLogNotice, "cdrom %s", Lines[i]);
    }

    // The same treatment for the emulator: replaced rather than edited, because
    // a volume that stopped being universal has to lose its line as well.
    const unsigned nEngines = ChooserEngineLines (pChooser, Lines, CHOOSER_MAX);
    while (PrefsFindString ("engine", 0) != 0)
    {
        PrefsRemoveItem ("engine", 0);
    }
    for (unsigned i = 0; i < nEngines; i++)
    {
        PrefsAddString ("engine", Lines[i]);
        HostLog (FROM, HostLogNotice, "engine %s", Lines[i]);
    }
    SavePrefs ();
}

// The parameter RAM, forgotten. main.cpp:106 rebuilds it from scratch as soon
// as the "NuMc" signature is missing, so removing the file *is* the zap — and
// it touches nothing of the user's data. Each engine keeps its own file
// (xpram_circle.cpp), and the firmware serves both, so it is told which.
//
// HostFileRemove and not remove(): on the Pi the newlib glue's remove() deletes
// the file and then leaves something behind that wedges the next fopen — the
// ROM never opened and the kernel stopped dead, with no log at all.
static void ForgetPramFile (const char *pWhy, const char *pFile)
{
    char Path[40];
    snprintf (Path, sizeof Path, "/%s", pFile);
    const THostRemove Result = HostFileRemove (Path);
    HostLog (FROM, HostLogNotice, "%s: %s %s", pWhy, pFile,
             Result == HostRemoved ? "forgotten"
                                   : (Result == HostAbsent ? "was already absent"
                                                           : "could not be removed"));
}

static void ForgetPram (const char *pWhy, bool b68k, bool bPowerPC)
{
    if (b68k)
    {
        ForgetPramFile (pWhy, "BasiliskII_XPRAM");
    }
    if (bPowerPC)
    {
        ForgetPramFile (pWhy, "SheepShaver_XPRAM");
    }
}

/*
 *  The settings, between the card and the screen
 *
 *  The screen is handed values and answers values; everything that knows what a
 *  preferences file looks like is here. Which is the same division the chooser
 *  already has, and the reason both of them can be driven by synthetic events
 *  on a development machine.
 */
// What a card carries when nothing says otherwise, and what an empty path field
// falls back to rather than silently offering no volume at all.
static const char SHARED_PATH[] = "/shared";

// What an emptied server field falls back to: the preferences' own default.
static const char TIME_SERVER[] = "pool.ntp.org";

// The names TSoundOutput carries in the file, in its own order. One table, read
// by both directions: two would drift.
static const char *const SOUND_NAMES[SoundOutputCount] = { "off", "hdmi", "jack", "usb" };

static void LoadSettings (TSettings *pSettings)
{
    // Snapped to what the screen can offer: a card naming a size the menu does
    // not have would otherwise be shown as the nearest one and saved as itself,
    // which is the interface lying about what the button will do.
    const int32 nRAM = PrefsFindInt32 ("ramsize");
    pSettings->V.nMemoryMB  = SettingsMemoryOffered ((unsigned) (nRAM / (1024 * 1024)));
    pSettings->V.nMouseDpi  = PrefsFindInt32 ("mousedpi");

    // Silence is what an unreadable value means, and nosound wins wherever the
    // two disagree — audio_circle.cpp reads them the same way. A device claimed
    // and not working is what froze the guest once already, and cost a card.
    const char *pSound = PrefsFindString ("soundoutput");
    pSettings->V.nSound = SoundOff;
    if (pSound != 0 && !PrefsFindBool ("nosound"))
    {
        for (unsigned i = 0; i < SoundOutputCount; i++)
        {
            if (strcmp (SOUND_NAMES[i], pSound) == 0)
            {
                pSettings->V.nSound = i;
                break;
            }
        }
    }

    pSettings->V.nLanguage = (unsigned) StringsLanguage ();
    pSettings->V.bBootMenu = PrefsFindBool ("bootmenu");

    // Both read at every start of the Macintosh (basilisk_glue.cpp for jit,
    // quickdraw_circle.cpp for qdaccel), so neither owes the board a restart.
    // Anything but "off" reads as ticked: the file may hold parallel, probe or
    // verify, which StoreSettings() leaves alone unless the box is changed.
    pSettings->V.bJit = PrefsFindBool ("jit");
    const char *pQuickDraw = PrefsFindString ("qdaccel");
    pSettings->V.bQuickDraw = pQuickDraw != 0 && pQuickDraw[0] != '\0'
                           && strcmp (pQuickDraw, "off") != 0;
#ifdef OKAPIA_TRANSLATOR
    pSettings->bJitAvailable = true;
#else
    pSettings->bJitAvailable = false;
#endif

    const char *pShared = PrefsFindString ("extfs");
    pSettings->V.bShared = pShared != 0 && pShared[0] != '\0';
    pSettings->V.SharedPath[0] = '\0';
    // The path it would use is kept even when it is off, so that turning it back
    // on does not lose where it was.
    StrAppend (pSettings->V.SharedPath, SETTINGS_PATH, 0,
               pShared != 0 && pShared[0] != '\0' ? pShared : SHARED_PATH);
    const char *pName = PrefsFindString ("extfsname");
    pSettings->V.SharedName[0] = '\0';
    StrAppend (pSettings->V.SharedName, SETTINGS_NAME, 0, pName != 0 ? pName : "");

    // What this machine can actually do: an output offered and then silent
    // would be the firmware lying (okapia_host_circle.cpp says which a Pi has).
    pSettings->nSoundAvailable = HostSoundOutputs ();

    pSettings->Opened = pSettings->V;
}

// Into the preferences in memory; ApplyVolumes() writes the file once for
// everything the window changed.
static void StoreSettings (const TSettings *pSettings)
{
    PrefsReplaceInt32 ("ramsize", (int32) pSettings->V.nMemoryMB * 1024 * 1024);
    PrefsReplaceInt32 ("mousedpi", pSettings->V.nMouseDpi);

    PrefsReplaceString ("soundoutput", SOUND_NAMES[pSettings->V.nSound]);
    // Kept in step rather than replaced: upstream code and every prefs file
    // ever written for a desktop Basilisk II reads nosound, and a card carried
    // to one of those should still be silent when it was silent here.
    PrefsReplaceBool ("nosound", pSettings->V.nSound == SoundOff);

    PrefsReplaceString ("language", StringsCode ((TLanguage) pSettings->V.nLanguage));
    PrefsReplaceBool ("bootmenu", pSettings->V.bBootMenu);
    PrefsReplaceBool ("jit", pSettings->V.bJit);
    if (pSettings->V.bQuickDraw != pSettings->Opened.bQuickDraw)
    {
        PrefsReplaceString ("qdaccel", pSettings->V.bQuickDraw ? "sync" : "off");
    }
    PrefsReplaceString ("extfs", pSettings->V.bShared
                                 ? (pSettings->V.SharedPath[0] != '\0'
                                    ? pSettings->V.SharedPath : SHARED_PATH)
                                 : "");
    PrefsReplaceString ("extfsname", pSettings->V.SharedName);

    HostLog (FROM, HostLogNotice,
                            "Settings: %u MB, sound %s, language %s, %s, %s, "
                            "jit %s, qdaccel %s",
                            pSettings->V.nMemoryMB,
                            SOUND_NAMES[pSettings->V.nSound],
                            StringsCode ((TLanguage) pSettings->V.nLanguage),
                            pSettings->V.bShared ? pSettings->V.SharedPath
                                                 : "no shared folder",
                            pSettings->V.bBootMenu ? "boot menu always"
                                                   : "boot menu on Option",
                            pSettings->V.bJit ? "on" : "off",
                            PrefsFindString ("qdaccel") != 0 ? PrefsFindString ("qdaccel")
                                                             : "off");
}

/*
 *  The date and time page
 */
static void LoadDateTime (TDateTime *p)
{
    // The zone the card names, the fixed offset of an older card as itself, and
    // UTC when there is neither — what the clock itself does (clock_circle.cpp).
    const int nZone = ClockZoneFind (PrefsFindString ("zone"));
    p->V.nFixedMinutes = (int) PrefsFindInt32 ("timezone");
    p->V.nZone = nZone >= 0 ? (unsigned) nZone
               : (p->V.nFixedMinutes != 0 ? ClockZoneCount () : (unsigned) ClockZoneFind ("utc"));

    // As the shared folder's path: the server is kept when the box is off, so
    // ticking it again does not lose which one it was.
    const char *pServer = PrefsFindString ("ntp");
    p->V.bNetTime = pServer != 0 && pServer[0] != '\0';
    p->V.Server[0] = '\0';
    StrAppend (p->V.Server, DATETIME_SERVER, 0, p->V.bNetTime ? pServer : TIME_SERVER);
    p->bHostClock = HostClockIsHosts ();
    p->Opened = p->V;
}

static const char *ServerOf (const TDateTime *p)
{
    return !p->V.bNetTime ? "" : (p->V.Server[0] != '\0' ? p->V.Server : TIME_SERVER);
}

// What the page shows is what the clock does: both are told at once, so the
// hour on the page moves with the zone chosen.
static void ApplyDateTime (const TDateTime *p)
{
    if (p->V.nZone < ClockZoneCount ())
    {
        HostClockSetZone (ClockZoneKey (p->V.nZone), 0);
    }
    else
    {
        HostClockSetZone ("", p->V.nFixedMinutes);
    }
    HostClockSetServer (ServerOf (p));
}

// Into the preferences in memory; ApplyVolumes() writes the file.
static void StoreDateTime (const TDateTime *p)
{
    // A city replaces the fixed offset for good; keeping the fixed offset
    // touches neither key.
    if (p->V.nZone < ClockZoneCount ())
    {
        PrefsReplaceString ("zone", ClockZoneKey (p->V.nZone));
        PrefsReplaceInt32 ("timezone", 0);
    }
    PrefsReplaceString ("ntp", ServerOf (p));
    ApplyDateTime (p);
    HostLog (FROM, HostLogNotice, "Date and time: zone %s, time server %s",
                            p->V.nZone < ClockZoneCount () ? ClockZoneKey (p->V.nZone) : "fixed",
                            p->V.bNetTime ? ServerOf (p) : "none");
}

// The hour, and the sentence saying where it came from, with the offset in
// force — which is how summer time shows itself.

static void DateTimeNow (TDateTime *p)
{
    p->nLocal = HostLocalTime ();
    const char *pDetail = "";
    const THostClockSource Source = HostClockSource (&pDetail);
    unsigned n = 0;
    p->Source[0] = '\0';
    switch (Source)
    {
    case HostClockNetwork:
        n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromNtp));
        n = StrAppend (p->Source, sizeof p->Source, n, " ");
        n = StrAppend (p->Source, sizeof p->Source, n, pDetail);
        break;

    case HostClockMac:
        n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromMac));
        break;

    case HostClockHost:
        n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromHost));
        break;

    case HostClockVolume:
        n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromVolume));
        n = StrAppend (p->Source, sizeof p->Source, n, " ");
        n = StrAppend (p->Source, sizeof p->Source, n, pDetail);
        break;

    default:
        n = StrAppend (p->Source, sizeof p->Source, 0, Str (StrClockFromBuild));
        break;
    }
    n = StrAppend (p->Source, sizeof p->Source, n, " · ");
    ClockOffsetText (p->Source + n, sizeof p->Source - n, HostTimeZoneMinutes ());
}

/*
 *  What Okapia found, for the information pane
 *
 *  Every line of it already goes out of the serial port at startup. The serial
 *  port is a wire nobody has once the card is in the Pi, which is the same
 *  problem the preferences file has: information held behind a connection.
 */
static void GatherInfo (TInfo *pInfo, const TChooser *pChooser, const TSurface *pOutput)
{
    char Line[INFO_VALUE];
    unsigned n;

    pInfo->nCount = 0;
    InfoAdd (pInfo, Str (StrInfoVersion), OKAPIA_VERSION);
    const bool bDesktop = HostKind () == HostDesktop;
    InfoAdd (pInfo, Str (bDesktop ? StrInfoComputer : StrInfoMachine), HostMachineName ());

    n = StrAppendNumber (Line, sizeof Line, 0, pOutput->nWidth);
    n = StrAppend (Line, sizeof Line, n, "x");
    n = StrAppendNumber (Line, sizeof Line, n, pOutput->nHeight);
    StrAppend (Line, sizeof Line, n, ", 32 bpp");
    InfoAdd (pInfo, Str (StrInfoDisplay), Line);

    // Measured from the file, not from ROMSize: the ROM is loaded when the
    // Macintosh starts, which has not happened yet — this pane runs before it,
    // every time, and a zero there would be a lie about the card.
    const char *pROM = PrefsFindString ("rom");
    n = StrAppend (Line, sizeof Line, 0, pROM != 0 ? pROM : Str (StrInfoNone));
    if (pROM != 0)
    {
        FILE *pFile = fopen (CardPath (pROM), "rb");
        if (pFile != 0)
        {
            fseek (pFile, 0, SEEK_END);
            const long nSize = ftell (pFile);
            fclose (pFile);
            n = StrAppend (Line, sizeof Line, n, ", ");
            n = StrAppendNumber (Line, sizeof Line, n, (unsigned long) (nSize / 1024));
            StrAppend (Line, sizeof Line, n, " KB");
        }
        else
        {
            StrAppend (Line, sizeof Line, n, " — ?");
        }
    }
    InfoAdd (pInfo, Str (StrInfoRom), Line);

    // The number with the machine it stands for, because "5" tells nobody
    // anything and it is the line one reads when a System refuses to start. Only
    // the two this project actually sets are named: a table of machine
    // identifiers written from memory is exactly the kind of period fact that
    // turns out wrong, and a bare number is honest where a wrong name is not.
    const int32 nModel = PrefsFindInt32 ("modelid");
    const char *pName = nModel == 5  ? "Mac IIci"
                      : nModel == 14 ? "Quadra 900"
                                     : 0;
    n = 0;
    if (pName != 0)
    {
        n = StrAppend (Line, sizeof Line, 0, pName);
        n = StrAppend (Line, sizeof Line, n, " (");
        n = StrAppendNumber (Line, sizeof Line, n, (unsigned long) nModel);
        n = StrAppend (Line, sizeof Line, n, ")");
    }
    else
    {
        n = StrAppendNumber (Line, sizeof Line, 0, (unsigned long) nModel);
    }
    n = StrAppend (Line, sizeof Line, n, ", ");
    StrAppend (Line, sizeof Line, n, PrefsFindBool ("modelidauto") ? Str (StrInfoModelAuto)
                                                                  : Str (StrInfoModelPref));
    InfoAdd (pInfo, Str (StrInfoModel), Line);

    // The card itself, in the units the user bought it in. FatFs counts free
    // clusters rather than bytes, so the size comes back as a cluster count and
    // is turned into megabytes here — where the rounding is visible.
    Line[0] = '\0';
    {
        unsigned long nTotalMB = 0, nFreeMB = 0;
        if (HostCardSpace (&nTotalMB, &nFreeMB))
        {
            n = StrAppendNumber (Line, sizeof Line, 0, nTotalMB);
            n = StrAppend (Line, sizeof Line, n, " ");
            n = StrAppend (Line, sizeof Line, n, Str (StrUnitMb));
            n = StrAppend (Line, sizeof Line, n, ", ");
            n = StrAppendNumber (Line, sizeof Line, n, nFreeMB);
            n = StrAppend (Line, sizeof Line, n, " ");
            StrAppend (Line, sizeof Line, n, Str (StrMbFree));
        }
        else
        {
            StrAppend (Line, sizeof Line, 0, Str (StrInfoNone));
        }
    }
    InfoAdd (pInfo, Str (bDesktop ? StrInfoFolder : StrInfoCard), Line);

    // The startup volume as the chooser understands it, which is the first disk
    // line — the order is the setting, there is no other place it is written.
    Line[0] = '\0';
    if (pChooser->nStartup >= 0)
    {
        const TChooserVolume *v = &pChooser->Volumes[pChooser->nStartup];
        n = StrAppend (Line, sizeof Line, 0, v->Name);
        n = StrAppend (Line, sizeof Line, n, " (");
        n = StrAppend (Line, sizeof Line, n, v->Path);
        StrAppend (Line, sizeof Line, n, ")");
    }
    else
    {
        StrAppend (Line, sizeof Line, 0, Str (StrInfoNone));
    }
    InfoAdd (pInfo, Str (StrInfoStartup), Line);

    /*
     *  What Okapia is made of
     *
     *  The combined work can only be distributed under the strictest licence in
     *  it (§5), so the pane owes its components a place: a machine that says
     *  nothing about what it is made of is a machine asking to be taken on
     *  trust. Every line here is copied from the README and from
     *  scripts/fetch-fonts.sh, which are where these facts are established —
     *  a licence written from memory is a licence stated wrongly.
     */
    pInfo->nCredits = 0;
    InfoCredit (pInfo, "Okapia",      "GPLv3+ · " OKAPIA_AUTHOR);
    InfoCredit (pInfo, "Basilisk II", "GPLv2+ · Christian Bauer et al., kanjitalk755");
    InfoCredit (pInfo, "SheepShaver", "GPLv2+ · Christian Bauer et al., kanjitalk755");
    InfoCredit (pInfo, "Circle",      "GPLv3+ · Rene Stange");
    InfoCredit (pInfo, "libhfs",      "GPLv2+ · Robert Leslie");
    InfoCredit (pInfo, "Helvetica",   "X11 · Adobe Systems, Digital Equipment");
}

/*
 *  One page, for as long as the user is on it
 *
 *  Everything about painting a frame lives in ScreenPresent, so what is left
 *  here is the loop itself: take the events, hand them to the screen, and ask
 *  the page what a control meant. Four screens share it, which is why it takes
 *  its page as three functions rather than knowing any of them.
 */
// Which column of a list the loop last reported, for the one page that has any.
// It rides beside the index rather than inside it because every other page's
// Operate takes a component and nothing else, and widening all of them for one
// would be the tail wagging the dog.
static unsigned s_nCell;

struct TPageDriver
{
    void     (*Repaint) (TSurface *);
    unsigned (*Widgets) (TWidget **);
    // 0 to stay on the page. Negative means the page changed shape and has to
    // be laid out again — the language does that, its labels not being the same
    // width twice.
    int      (*Operate) (int nIndex);
    void     (*Changed) (TScreen *);    // selection or text changed; may be 0
    void     (*Relayout) (TSurface *);  // may be 0 when nothing can move
    // What Escape means on this page, or 0 when it means nothing. A dialogue
    // with two answers has a refusal and the key for it is Escape, exactly as
    // Return is the key for the assent: neither needs the keyboard to be
    // anywhere in particular, which is why an alert offers no navigation at
    // all.
    int      (*Cancel) (void);
    // Asked four times a second, for a page whose model changes on its own —
    // the networks in range, a link coming up. True when it did, and the page
    // is drawn again. May be 0.
    bool     (*Tick) (void);
};

static const int PageRelayout = -1;
// The host asked to quit (a desktop's window closed) on a page with no refusal.
static const int PageQuit = -2;

static int RunPage (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                    const TPageDriver *pDriver)
{
    TWidget *pWidgets = 0;
    unsigned nCount = pDriver->Widgets (&pWidgets);

    TScreen Screen;
    ScreenInit (&Screen, pTheme, pWidgets, nCount);
    Screen.Background = ColorWhite;
    Screen.Bounds     = Rect (0, 0, pOutput->nWidth, pOutput->nHeight);
    // The page under the pointer is gone, so what was saved under it is gone
    // too — putting it back would stamp a piece of the last screen onto this
    // one, exactly where the eye already is.
    GfxCursorForget ();
    pDriver->Repaint (pShadow);
    GfxBlit (pOutput, pShadow, Screen.Bounds);
    TRect Ignored;
    ScreenPaintDirty (pShadow, &Screen, &Ignored);
    HostOutputPresent (0, 0, pOutput->nWidth, pOutput->nHeight);

    int nX = 0, nY = 0;
    bool bPointer = false;
    int nResult = 0;

    for (unsigned nTick = 0; nResult == 0; nTick++)
    {
        TEvent Event;
        bool bChanged = false;
        while (HostInputNext (&Event))
        {
            if (Event.Type == EventMouseMove)
            {
                bPointer = true;
            }
            const TScreenReply Reply = ScreenEvent (&Screen, &Event);
            if (Reply.Result == ScreenChanged)
            {
                bChanged = true;
                if (pDriver->Changed != 0)
                {
                    pDriver->Changed (&Screen);
                }
            }
            else if (Reply.Result == ScreenCancelled && pDriver->Cancel != 0)
            {
                const int nAnswer = pDriver->Cancel ();
                if (nAnswer != 0)
                {
                    nResult = nAnswer;
                }
            }
            else if (Reply.Result == ScreenActivated)
            {
                bChanged = true;
                s_nCell = Reply.nCell;
                const int nAnswer = pDriver->Operate (Reply.nIndex);
                if (nAnswer == PageRelayout && pDriver->Relayout != 0)
                {
                    // The focus keeps its place: the components are rebuilt in
                    // the same order, so the index still names the same control
                    // even though its rectangle has moved.
                    const int nFocus = Screen.nFocus;
                    GfxCursorForget ();
                    pDriver->Relayout (pShadow);
                    nCount = pDriver->Widgets (&pWidgets);
                    ScreenInit (&Screen, pTheme, pWidgets, nCount);
                    Screen.Background = ColorWhite;
                    Screen.Bounds     = Rect (0, 0, pOutput->nWidth, pOutput->nHeight);
                    // Where the hand was, not where the layout would start it:
                    // a tabbed page declares Start, and the language popup
                    // that caused this is three rows away from it.
                    ScreenFocus (&Screen, nFocus);
                }
                else if (nAnswer != 0 && nAnswer != PageRelayout)
                {
                    nResult = nAnswer;
                }
                // A control that only changed the model may have changed what
                // the rows say, so the screen is drawn again whole rather than
                // guessed at.
                Screen.bDirtyAll = true;
            }
        }

        if (pDriver->Tick != 0 && nTick % 25 == 0 && pDriver->Tick ())
        {
            Screen.bDirtyAll = true;
            bChanged = true;
        }

        if (bChanged || (nTick % (500 / SLICE_MS) == 0 && ScreenBlinkCaret (&Screen))
            || bPointer)
        {
            HostInputPointer (&nX, &nY);
            ScreenPresent (pShadow, pOutput, &Screen, pDriver->Repaint, bPointer, nX, nY,
                           pTheme->nIconScale);
            HostOutputPresent (0, 0, pOutput->nWidth, pOutput->nHeight);
        }
        // The Wi-Fi's tasks run only when this thread lets them, and a menu
        // left open must not cost the connection. Frames are dropped meanwhile.
        HostPoll ();
        FwSleep (10);
        if (HostQuitRequested () && pDriver->Cancel != 0)
        {
            // The window closed under the menu: the page's refusal first, and
            // the window's loop then answers Shut Down.
            const int nAnswer = pDriver->Cancel ();
            nResult = nAnswer != 0 ? nAnswer : PageQuit;
        }
        else if (HostQuitRequested ())
        {
            nResult = PageQuit;
        }
    }
    return nResult;
}

/*
 *  The four screens, and the models they work on
 *
 *  Static because the page driver takes plain functions and not closures: there
 *  is one firmware, it shows one page at a time, and a screen that outlived its
 *  model would be drawing freed memory.
 */
static TChooser  s_Chooser;

TFirmwareEngine FirmwareWantedEngine (void)
{
    return ChooserStartupEngine (&s_Chooser) == CPUPowerPC ? FirmwareEnginePowerPC
                                                           : FirmwareEngine68k;
}
static TSettings  s_Settings;
static TDisplaySettings s_Display;
static TNetwork   s_Network;
static TInfo      s_Info;
// Where the keyboard was when the footer's tick box was pressed: pressing it
// lays the window out again, and the hand should find it where it left it.
static bool       s_bFocusBootMenu;
static TConfirm   s_Confirm;
static TNewVolume s_NewVolume;

/*
 *  The network, between the board and the page
 *
 *  The page is handed what the board found and answers what was asked; this is
 *  where the Wi-Fi is started, scanned and told which network to join, and
 *  where `ether` and the three `wifi` preferences are read and written.
 */
static const char *const ETHER_NAMES[NetworkKindCount] = { "", "eth", "wlan" };

// Set when the page has asked for a connection and the board has none yet. The
// start happens on the next tick, after "Starting the Wi-Fi…" is on the
// screen: loading the chip's firmware holds this thread for a second or two.
static bool s_bNetStartPending;
static unsigned s_nNetTicks;

static unsigned RunningKind (void)
{
    return HostNetRunning ();
}

static void LoadNetwork (TNetwork *p)
{
    memset (p, 0, sizeof *p);
    switch (NetEtherKindOf (PrefsFindString ("ether")))
    {
    case NetEtherWired: p->V.nKind = NetworkCable; break;
    case NetEtherWLAN:  p->V.nKind = NetworkWiFi;  break;
    default:            p->V.nKind = NetworkOff;   break;
    }
    NetCopyValue (p->V.SSID, sizeof p->V.SSID, PrefsFindString ("wifissid"));
    NetCopyValue (p->V.Password, sizeof p->V.Password, PrefsFindString ("wifipassword"));
    NetCopyValue (p->V.Country, sizeof p->V.Country, PrefsFindString ("wificountry"));
    for (char *c = p->V.Country; *c != '\0'; c++)
    {
        if (*c >= 'a' && *c <= 'z')
        {
            *c = (char) (*c - 'a' + 'A');
        }
    }
    if (p->V.Country[0] == '\0')
    {
        // The supplicant needs one. The language is the only hint there is,
        // and it is shown on the page to be corrected.
        strcpy (p->V.Country, StringsLanguage () == LangFR ? "FR" : "US");
    }

    p->nAvailable = HostNetAvailable ();
    p->nRunning   = RunningKind ();
    // A start that failed is final for this boot: Circle cannot give a device
    // back to try again.
    p->State = HostNetTried () && p->nRunning == NetworkOff ? NetStateUnavailable
                                                             : NetStateIdle;
    p->Opened = p->V;

    // The connection the card names, started when the page is first turned to
    // (the tick runs on that page only): a Wi-Fi page that waits for a click
    // on a radio already ticked lists nothing — seen on the Pi 4.
    s_bNetStartPending = false;
    if (p->V.nKind != NetworkOff && !HostNetTried ()
        && (p->nAvailable & (1u << p->V.nKind)) != 0)
    {
        p->State = p->V.nKind == NetworkWiFi ? NetStateStarting : NetStateIdle;
        s_bNetStartPending = true;
    }
}

static bool NetworkChanged (const TNetwork *p)
{
    return p->V.nKind != p->Opened.nKind
        || strcmp (p->V.SSID, p->Opened.SSID) != 0
        || strcmp (p->V.Password, p->Opened.Password) != 0
        || strcmp (p->V.Country, p->Opened.Country) != 0;
}

static void StoreValue (const char *pName, const char *pValue)
{
    // An empty value is no line at all: a card that never had Wi-Fi does not
    // gain three blank ones because the page was opened.
    if (pValue[0] == '\0')
    {
        while (PrefsFindString (pName, 0) != 0)
        {
            PrefsRemoveItem (pName, 0);
        }
    }
    else
    {
        PrefsReplaceString (pName, pValue);
    }
}

// Into the preferences in memory, which is also what HostNetStart() reads.
// Opened follows: it stands for what the preferences hold, so SaveAll() can
// tell a change made since. Left at the card's values, a cable started from
// the page and the Wi-Fi chosen again compared equal to the card, nothing was
// stored, and the file was written with the cable's `ether eth` — seen on the
// Pi 4.
static void StoreNetwork (TNetwork *p)
{
    StoreValue ("ether", ETHER_NAMES[p->V.nKind]);
    StoreValue ("wifissid", p->V.SSID);
    StoreValue ("wifipassword", p->V.Password);
    StoreValue ("wificountry", p->V.Country);
    // Never the password, in any form.
    HostLog (FROM, HostLogNotice, "Network: ether \"%s\", Wi-Fi \"%s\", %s, country %s",
                            ETHER_NAMES[p->V.nKind], p->V.SSID,
                            p->V.Password[0] == '\0' ? "no password" : "a password",
                            p->V.Country);
    p->Opened = p->V;
}

// The connection chosen, started now if the board has none yet. Once one is
// running the page says a restart is due instead.
static void NetworkStartWanted (TNetwork *p)
{
    if (HostNetTried () || p->V.nKind == NetworkOff
        || (p->nAvailable & (1u << p->V.nKind)) == 0)
    {
        return;
    }
    StoreNetwork (p);
    p->State = p->V.nKind == NetworkWiFi ? NetStateStarting : NetStateIdle;
    s_bNetStartPending = true;
}

static void NetworkJoinNow (TNetwork *p)
{
    snprintf (p->Subject, sizeof p->Subject, "%s", p->V.SSID);
    if (p->nRunning == NetworkWiFi)
    {
        p->State = HostWlanApply (p->V.SSID, p->V.Password, p->V.Country) ? NetStateJoining
                                                                      : NetStateRefused;
        return;
    }
    NetworkStartWanted (p);
}

static bool NetworkTick (void)
{
    TNetwork *p = &s_Network;
    const TNetwork Was = *p;
    s_nNetTicks++;

    if (s_bNetStartPending)
    {
        s_bNetStartPending = false;
        const bool bOK = HostNetStart ();
        p->nRunning = RunningKind ();
        if (!bOK)
        {
            p->State = NetStateUnavailable;
        }
        else if (p->nRunning == NetworkWiFi)
        {
            HostWlanScan ();
        }
    }

    if (p->nRunning == NetworkWiFi)
    {
        static TNetworkSeen Seen[48];
        const unsigned nFound = HostWlanSeen (Seen, 48);
        p->nSeen = NetworkSortSeen (Seen, nFound);
        memcpy (p->Seen, Seen, p->nSeen * sizeof Seen[0]);

        char Subject[NETWORK_SSID];
        int nDbm = 0;
        p->State = HostWlanState (Subject, sizeof Subject, &nDbm, p->nSeen);
        p->nDbm  = nDbm;
        if (Subject[0] != '\0')
        {
            memcpy (p->Subject, Subject, sizeof p->Subject);
        }
        // Nobody else scans while nothing is joined, and a list that stops
        // changing reads as a list that is complete.
        if (s_nNetTicks % 40 == 0
            && (p->State == NetStateReady || p->State == NetStateLost
                || p->State == NetStateRefused || p->State == NetStateSearching))
        {
            HostWlanScan ();
        }
    }
    else if (p->nRunning == NetworkCable && s_nNetTicks % 4 == 0)
    {
        unsigned nMbps = 0;
        p->State = HostNetLinkUp (&nMbps) ? NetStateCableUp : NetStateCableDown;
        p->nMbps = nMbps;
    }

    if (memcmp (&Was, p, sizeof Was) == 0)
    {
        return false;
    }
    NetworkSync (p);
    return true;
}

/*
 *  The window
 */
static TPageTab s_Tab = PageTabSystem;
static bool     s_bFocusTabs;
static TSurface *s_pOutputForInfo;

static bool RestartWanted (void)
{
    return SettingsNeedsRestart (&s_Settings) || NetworkNeedsRestart (&s_Network);
}

static void LoadDisplay (TSurface *pOutput)
{
    s_Display = {};
    s_Display.Output = {pOutput->nWidth, pOutput->nHeight};
    THostOutputFacts Facts;
    HostOutputDescribe (&Facts);
    s_Display.Preferred = {Facts.nPreferredWidth, Facts.nPreferredHeight};
    StrAppend (s_Display.Monitor, sizeof s_Display.Monitor, 0, Facts.Monitor);
    s_Display.bConfigAvailable = Facts.bConfigAvailable;
    s_Display.bHdmiDirectives  = Facts.bHdmiDirectives;
    s_Display.bWindowed        = HostKind () == HostDesktop;
    s_Display.Start = VideoStartSize (PrefsFindString ("screen"), s_Display.Output);
    VideoPrefsRead (&s_Display.Modes);
    s_Display.nFrameSkip = PrefsFindInt32 ("frameskip");
}

/*
 *  The Journal page
 *
 *  The log as the host keeps it: on the Pi, the card's two files, padded to the
 *  sector (HostLogRead, okapia_host_circle.cpp).
 */
static TJournal s_Journal;

static void JournalChunk (const char *pText, unsigned nLength)
{
    JournalAppend (&s_Journal, pText, nLength);
}

static void LoadJournalText (void)
{
    JournalClear (&s_Journal);
    HostLogRead (s_Journal.bPrevious, JournalChunk);
}

static void LoadJournal (void)
{
    s_Journal = {};
    s_Journal.bSerial   = PrefsFindBool ("serial");
    s_Journal.bCounters = PrefsFindBool ("perfreport");
}

static int JournalAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
    {
        return nChrome;
    }
    if (JournalOperate (&s_Journal, nIndex) == JournalReload)
    {
        LoadJournalText ();
        return PageRelayout;
    }
    return 0;
}

static const int AnswerNewResolution = 2001;

static int DisplayAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
        return nChrome;
    return DisplayOperate (&s_Display, nIndex, s_nCell) ? AnswerNewResolution : PageAnswerNone;
}

static void NewResolutionChanged (TScreen *pScreen)
{
    NewResolutionSync (&s_Display, pScreen);
}

static int NewResolutionAdapter (int nIndex)
{
    return NewResolutionOperate (&s_Display, nIndex);
}

static int NewResolutionCancelled (void)
{
    return NewResolutionCancel;
}

static void RunNewResolution (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme)
{
    NewResolutionDraw (pShadow, &s_Display);
    static const TPageDriver Driver =
    {
        NewResolutionRepaint, NewResolutionWidgets, NewResolutionAdapter,
        NewResolutionChanged, 0, NewResolutionCancelled
    };
    RunPage (pOutput, pShadow, pTheme, &Driver);
}

static void DrawTab (TSurface *pSurface)
{
    PageChromeSet (RestartWanted (), s_bFocusTabs,
                   s_Chooser.nStartup >= 0 || RestartWanted ());
    PageChromeBootMenu (s_Settings.V.bBootMenu, s_bFocusBootMenu);
    PageChromeBootMenuHint (HostKind () == HostDesktop ? Str (StrBootMenuHintDesktop) : 0);
    switch (s_Tab)
    {
    case PageTabSystem:      ChooserDraw (pSurface, &s_Chooser);   break;
    case PageTabSettings:
        DateTimeNow (&s_Settings.Clock);
        SettingsDraw (pSurface, &s_Settings);
        break;
    case PageTabDisplay:     DisplayDraw (pSurface, &s_Display);   break;
    case PageTabJournal:     JournalDraw (pSurface, &s_Journal);   break;
    case PageTabNetwork:     NetworkDraw (pSurface, &s_Network);   break;
    default:
        // Gathered as the page is turned to: the startup volume it names is
        // whatever the first page says now.
        GatherInfo (&s_Info, &s_Chooser, s_pOutputForInfo);
        InfoDraw (pSurface, &s_Info);
        break;
    }
}

static void RelayoutTab (TSurface *pSurface)
{
    s_bFocusTabs = false;
    DrawTab (pSurface);
}

static int ChooserAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
    {
        return nChrome;
    }
    // ChooserNothing is zero, which is what the driver reads as "stay here".
    return (int) ChooserOperate (&s_Chooser, nIndex, s_nCell);
}

static void ChooserChanged (TScreen *)
{
    ChooserSync (&s_Chooser);
}

// Answered by the settings page and handled by the window, which owns the
// alerts. Far from the page chrome's and the chooser's answers.
static const int AnswerClockHelp = 2000;

static int SettingsAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
    {
        return nChrome;
    }
    switch (SettingsOperate (&s_Settings, nIndex))
    {
    case SettingsRelayout:
        return PageRelayout;

    case SettingsClockHelp:
        return AnswerClockHelp;

    case SettingsClock:
        // What the page shows is what the clock does: the hour moves with
        // the zone chosen.
        ApplyDateTime (&s_Settings.Clock);
        DateTimeNow (&s_Settings.Clock);
        DateTimeSync (&s_Settings.Clock);
        return 0;

    default:
        return 0;
    }
}

static int NetworkAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
    {
        return nChrome;
    }
    switch (NetworkOperate (&s_Network, nIndex))
    {
    case NetworkRelayout:
        return PageRelayout;

    case NetworkKindChanged:
        NetworkStartWanted (&s_Network);
        NetworkSync (&s_Network);
        break;

    case NetworkSearch:
        if (HostWlanScan ())
        {
            s_Network.State = NetStateSearching;
        }
        NetworkSync (&s_Network);
        break;

    case NetworkJoin:
        NetworkJoinNow (&s_Network);
        NetworkSync (&s_Network);
        break;

    default:
        break;
    }
    return 0;
}

static int InfoAdapter (int nIndex)
{
    return PageChromeAnswer (nIndex);
}

// The minute turned, the network answered, summer time began: the page again.
static bool SettingsTick (void)
{
    TDateTime *p = &s_Settings.Clock;
    const unsigned nMinute = p->nLocal / 60;
    char Was[sizeof p->Source];
    memcpy (Was, p->Source, sizeof Was);
    DateTimeNow (p);
    if (p->nLocal / 60 == nMinute && strcmp (Was, p->Source) == 0)
    {
        return false;
    }
    DateTimeSync (p);
    return true;
}

static int ConfirmAdapter (int nIndex)
{
    return (int) ConfirmOperate (nIndex);
}

// Escape is the refusal, and a message with nothing to refuse has none: an
// alert whose only answer is "I see" is dismissed by that answer alone.
static int ConfirmCancelled (void)
{
    return s_Confirm.pNo == 0 ? 0 : (int) ConfirmNo;
}

/*
 *  Making a volume
 *
 *  Three steps and one rule. The rule is that a volume which does not fit
 *  cannot be asked for, and the way that is enforced is by not offering the
 *  size: the screen picks from a list this file computes, so there is no
 *  refusal to write afterwards and no arithmetic repeated on both sides.
 *
 *  The file is created at its full size before anything is written into it.
 *  That is not about speed: an image whose clusters are already allocated
 *  cannot meet a full card halfway through a write the Macintosh believes has
 *  succeeded, which is the one failure this project refuses to ship (see
 *  docs/topics/storage.md). A volume that grows on demand would turn a full card into a
 *  lost write at the worst possible moment.
 */

// What the card must keep for itself: the two parameter RAMs, a preferences
// file that is rewritten in place at every pass through this screen — so it
// needs room to be written before the old one is gone — the shared folder, and
// enough slack that FatFs is never asked to allocate from nothing. A tenth of
// what is free, and never less than this, because a tenth of very little is
// nothing at all.
static const unsigned long CARD_MARGIN_MB = 64;

// The rungs offered, smallest first. It stops at 2 GB because HFS standard runs
// out of allocation blocks there: 65535 of them, and libhfs sizes the block
// from the volume (hfs.c:1573), so beyond that there is nothing left to give.
static const unsigned long SIZE_RUNGS[] = { 20, 40, 80, 160, 320, 640, 1024, 2048 };

static unsigned long CardFreeMB (void)
{
    unsigned long nTotalMB = 0, nFreeMB = 0;
    if (!HostCardSpace (&nTotalMB, &nFreeMB))
    {
        HostLog (FROM, HostLogError, "Cannot read the card's free space");
        return 0;
    }
    return nFreeMB;
}

// The file name is ours, not the user's: a Macintosh volume is named in
// MacRoman and may hold characters no FAT directory entry will take, so the
// name that is asked for is the one the Mac shows and this is the one the card
// carries. The first number nothing is using — a card whose volume1 was deleted
// gets volume1 back, which is what somebody counting their files expects.
static bool PickImagePath (char *pOut, size_t nSize)
{
    for (unsigned i = 1; i <= 99; i++)
    {
        snprintf (pOut, nSize, "/volume%u.image", i);
        if (!HostFileExists (pOut))
        {
            return true;
        }
    }
    return false;
}

// The sizes this card can actually offer, into the screen's own model.
static void OfferSizes (TNewVolume *pNew)
{
    pNew->nFreeMB = CardFreeMB ();
    const unsigned long nMargin = pNew->nFreeMB / 10 > CARD_MARGIN_MB
                                ? pNew->nFreeMB / 10 : CARD_MARGIN_MB;
    const unsigned long nRoom = pNew->nFreeMB > nMargin ? pNew->nFreeMB - nMargin : 0;

    pNew->nSizes = 0;
    for (unsigned i = 0; i < sizeof SIZE_RUNGS / sizeof SIZE_RUNGS[0]; i++)
    {
        if (SIZE_RUNGS[i] <= nRoom && pNew->nSizes < NEWVOLUME_SIZES)
        {
            pNew->SizeMB[pNew->nSizes++] = SIZE_RUNGS[i];
        }
    }
    // The largest that fits, because somebody making their first volume on a
    // card wants the card, and the ones who want less will say so.
    pNew->nPick = pNew->nSizes == 0 ? 0 : pNew->nSizes - 1;
}

static int NewVolumeAdapter (int nIndex)
{
    return (int) NewVolumeOperate (&s_NewVolume, nIndex);
}

static int NewVolumeCancelled (void)
{
    return (int) NewVolumeCancel;
}

static void RunNewVolume (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme)
{
    memset (&s_NewVolume, 0, sizeof s_NewVolume);
    snprintf (s_NewVolume.Name, sizeof s_NewVolume.Name, "%s", Str (StrUntitled));
    OfferSizes (&s_NewVolume);
    NewVolumeDraw (pShadow, &s_NewVolume);

    static const TPageDriver Driver =
    {
        NewVolumeRepaint, NewVolumeWidgets, NewVolumeAdapter, 0, 0, NewVolumeCancelled
    };
    if (RunPage (pOutput, pShadow, pTheme, &Driver) != (int) NewVolumeCreate)
    {
        return;
    }

    char Path[64];
    if (!PickImagePath (Path, sizeof Path))
    {
        HostLog (FROM, HostLogError, "No free image name on the card");
        return;
    }
    // Created at its full size before anything is written into it (see above):
    // HostFileCreate allocates the whole file or leaves nothing.
    const unsigned long nMB = s_NewVolume.SizeMB[s_NewVolume.nPick];
    if (!HostFileCreate (Path, nMB))
    {
        HostLog (FROM, HostLogError, "%s: cannot create %lu MB", Path, nMB);
        return;
    }
    // Formatted through the path the emulator will use, not the FatFs one:
    // libhfs reaches the card through newlib, as it does everywhere else here.
    if (!HfsFormat (Path, s_NewVolume.Name))
    {
        // Nothing of the user's is in it — it was made a moment ago — and a
        // file that is not a volume would show up in the inventory as nothing
        // at all, which is worse than not showing up.
        HostFileRemove (Path);
        return;
    }
    HostLog (FROM, HostLogNotice, "%s: created, %lu MB, \"%s\"",
                            Path, nMB, s_NewVolume.Name);

    // The card changed, so what the chooser is showing no longer describes it.
    GatherVolumes (&s_Chooser);
}

/*
 *  Asking before doing
 */
// With choices, *pnChoice is the one marked on the way in and the one marked
// when the assent came on the way out. AskThree() offers a third answer, at the
// left, and says which of the three came.
static TConfirmAnswer AskThree (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                                TConfirmLevel Level, TStringId Title, TStringId Body,
                                TStringId Yes, const char *pOther,
                                const TStringId *pChoices = 0, unsigned nChoices = 0,
                                unsigned *pnChoice = 0);

// A note with one answer: something to read, nothing to decide.
static void Tell (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                  TStringId Title, TStringId Body)
{
    s_Confirm.Level    = ConfirmNote;
    s_Confirm.pTitle   = Str (Title);
    s_Confirm.pBody    = Str (Body);
    s_Confirm.pYes     = Str (StrOk);
    s_Confirm.pNo      = 0;
    s_Confirm.pOther   = 0;
    s_Confirm.nChoices = 0;
    s_Confirm.nChoice  = 0;
    ConfirmDraw (pShadow, &s_Confirm);
    static const TPageDriver Driver =
    {
        ConfirmRepaint, ConfirmWidgets, ConfirmAdapter, 0, 0, ConfirmCancelled
    };
    RunPage (pOutput, pShadow, pTheme, &Driver);
}

static bool Ask (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                 TConfirmLevel Level, TStringId Title, TStringId Body, TStringId Yes,
                 const TStringId *pChoices = 0, unsigned nChoices = 0,
                 unsigned *pnChoice = 0)
{
    return AskThree (pOutput, pShadow, pTheme, Level, Title, Body, Yes, 0,
                     pChoices, nChoices, pnChoice) == ConfirmYes;
}

static TConfirmAnswer AskThree (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                                TConfirmLevel Level, TStringId Title, TStringId Body,
                                TStringId Yes, const char *pOther,
                                const TStringId *pChoices, unsigned nChoices,
                                unsigned *pnChoice)
{
    s_Confirm.Level  = Level;
    s_Confirm.pTitle = Str (Title);
    s_Confirm.pBody  = Str (Body);
    s_Confirm.pYes   = Str (Yes);
    s_Confirm.pNo    = Str (StrCancel);
    s_Confirm.pOther = pOther;
    s_Confirm.nChoices = pChoices == 0 ? 0 : nChoices;
    s_Confirm.nChoice  = pnChoice == 0 ? 0 : *pnChoice;
    for (unsigned i = 0; i < s_Confirm.nChoices && i < CONFIRM_CHOICES; i++)
    {
        s_Confirm.pChoices[i] = Str (pChoices[i]);
    }
    ConfirmDraw (pShadow, &s_Confirm);

    static const TPageDriver Driver =
    {
        ConfirmRepaint, ConfirmWidgets, ConfirmAdapter, 0, 0, ConfirmCancelled
    };
    const TConfirmAnswer Answer = (TConfirmAnswer) RunPage (pOutput, pShadow, pTheme, &Driver);
    if (pnChoice != 0)
    {
        *pnChoice = ConfirmChoice ();
    }
    return Answer;
}

/*
 *  The window, for as long as the user wants it
 */
// Everything the window changed, into the file once. Called when the Macintosh
// starts and when the board shuts down: the pages are a control panel's, and a
// control panel keeps what was set in it.
static void SaveAll (void)
{
    if (memcmp (&s_Settings.V, &s_Settings.Opened, sizeof s_Settings.V) != 0)
    {
        StoreSettings (&s_Settings);
    }
    if (NetworkChanged (&s_Network))
    {
        StoreNetwork (&s_Network);
    }
    if (memcmp (&s_Settings.Clock.V, &s_Settings.Clock.Opened, sizeof s_Settings.Clock.V) != 0)
    {
        StoreDateTime (&s_Settings.Clock);
    }
    if (s_Display.bModesChanged)
        VideoPrefsWrite (s_Display.Modes);
    if (s_Display.bRefreshChanged)
        PrefsReplaceInt32 ("frameskip", s_Display.nFrameSkip);
    if (s_Display.bStartChanged)
    {
        char Screen[32];
        snprintf (Screen, sizeof Screen, "win/%u/%u", s_Display.Start.nWidth, s_Display.Start.nHeight);
        PrefsReplaceString ("screen", Screen);
    }
    if (s_Journal.bSerialChanged)
        PrefsReplaceBool ("serial", s_Journal.bSerial);
    if (s_Journal.bCountersChanged)
        PrefsReplaceBool ("perfreport", s_Journal.bCounters);
    ApplyVolumes (&s_Chooser);          // and SavePrefs(), once
}

static TFirmwareResult RunScreens (TSurface *pOutput, const TTheme *pTheme)
{
    // The card was read before the window opened, because whether there is
    // anything to start decides whether the window opens at all.
    //
    // Drawn beside the screen and copied forward in one pass: painting into the
    // visible buffer puts the ground down before the control that stands on it,
    // and at sixty refreshes a second that is seen.
    TSurface Shadow;
    Shadow.nWidth  = pOutput->nWidth;
    Shadow.nHeight = pOutput->nHeight;
    Shadow.nPitch  = pOutput->nWidth * (unsigned) sizeof (unsigned);
    Shadow.pPixels = new unsigned char[(size_t) Shadow.nPitch * Shadow.nHeight];
    if (Shadow.pPixels == 0)
    {
        HostLog (FROM, HostLogError, "No room for a shadow surface");
        return FirmwareBoot;
    }

    LoadSettings (&s_Settings);
    LoadDisplay (pOutput);
    LoadJournal ();
    LoadNetwork (&s_Network);
    LoadDateTime (&s_Settings.Clock);
    s_pOutputForInfo = pOutput;
    s_Tab = PageTabSystem;
    s_bFocusTabs = false;

    static const TPageDriver Drivers[PageTabCount] =
    {
        { ChooserRepaint,  ChooserWidgets,  ChooserAdapter,  ChooserChanged, RelayoutTab, 0, 0 },
        { SettingsRepaint, SettingsWidgets, SettingsAdapter, 0,              RelayoutTab, 0,
          SettingsTick },
        { NetworkRepaint,  NetworkWidgets,  NetworkAdapter,  0,              RelayoutTab, 0,
          NetworkTick },
        { InfoRepaint,     InfoWidgets,     InfoAdapter,     0,              RelayoutTab, 0, 0 },
        { DisplayRepaint,  DisplayWidgets,  DisplayAdapter,  0,              RelayoutTab, 0, 0 },
        { JournalRepaint,  JournalWidgets,  JournalAdapter,  0,              RelayoutTab, 0, 0 }
    };

    TFirmwareResult Result = FirmwareBoot;
    bool bDone = false;
    while (!bDone)
    {
        DrawTab (&Shadow);
        const int nAnswer = RunPage (pOutput, &Shadow, pTheme, &Drivers[s_Tab]);
        s_bFocusTabs = false;
        s_bFocusBootMenu = false;

        if (nAnswer >= PageAnswerTab && nAnswer < PageAnswerTab + (int) PageTabCount)
        {
            // The page turns and the keyboard stays on the strip, so the arrows
            // go on turning pages.
            s_Tab = (TPageTab) (nAnswer - PageAnswerTab);
            s_bFocusTabs = true;
            if (s_Tab == PageTabJournal)
            {
                // Read when the page is turned to, so it is as new as the
                // moment it is looked at; not at every layout.
                LoadJournalText ();
            }
            continue;
        }

        switch (nAnswer)
        {
        case PageQuit:
            // The host's window closed: what Shut Down does, without asking,
            // since nothing has started and nothing can be lost.
            SaveAll ();
            Result = FirmwareHalt;
            bDone  = true;
            break;

        case AnswerNewResolution:
            RunNewResolution (pOutput, &Shadow, pTheme);
            break;

        case AnswerClockHelp:
            Tell (pOutput, &Shadow, pTheme, StrClockGroup, StrClockHelp);
            break;

        case PageAnswerBootMenu:
            s_Settings.V.bBootMenu = !s_Settings.V.bBootMenu;
            s_bFocusBootMenu = true;
            break;

        case PageAnswerStart:
            SaveAll ();
            Result = RestartWanted () ? FirmwareReboot : FirmwareBoot;
            bDone  = true;
            break;

        case PageAnswerShutDown:
            // A note: the Macintosh has not started, so nothing is open and
            // nothing can be lost. A caution mark here would be crying wolf,
            // and the next one would be believed a little less.
            //
            // Restart beside it: nothing is lost either way, and the board
            // comes back to this window — what Save and restart does.
            switch (AskThree (pOutput, &Shadow, pTheme, ConfirmNote, StrShutDownTitle,
                              StrShutDownBody, StrShutDown, Str (StrRestart)))
            {
            case ConfirmYes:
                SaveAll ();
                Result = FirmwareHalt;
                bDone  = true;
                break;

            case ConfirmOther:
                SaveAll ();
                Result = FirmwareReboot;
                bDone  = true;
                break;

            default:
                break;
            }
            break;

        case PageAnswerForgetPram:
            {
                // A caution: it changes something the Macintosh will not get
                // back, even though it touches none of the user's files.
                //
                // Which Macintosh, because each keeps its own. The one marked
                // first is the one the selected volume starts — what Command-
                // Option-P-R forgets — so accepting straight away does the same
                // as the keys.
                static const TStringId Which[] =
                {
                    StrForgetPram68k, StrForgetPramPowerpc, StrForgetPramBoth
                };
                enum { Which68k, WhichPowerPC, WhichBoth };
                unsigned nWhich = FirmwareWantedEngine () == FirmwareEnginePowerPC
                                ? WhichPowerPC : Which68k;
                if (Ask (pOutput, &Shadow, pTheme, ConfirmCaution, StrForgetPramTitle,
                         StrForgetPramBody, StrForgetPram,
                         Which, sizeof Which / sizeof Which[0], &nWhich))
                {
                    ForgetPram ("Boot menu", nWhich != WhichPowerPC, nWhich != Which68k);
                }
            }
            break;

        case (int) ChooserNewVolume:
            if (s_Tab == PageTabSystem)
            {
                RunNewVolume (pOutput, &Shadow, pTheme);
            }
            break;

        default:
            break;
        }
    }

    delete[] Shadow.pPixels;
    return Result;
}

TFirmwareResult FirmwareRun (TFirmwareEngine Built)
{
    // Before the window, once per power-on, and only when `netbench` asks:
    // the network measured with nothing else running.
    HostMenuPrelude ();

    // On the Pi, claimed once for the life of the board and lent out, never
    // taken and given back: this runs again after every restart from Mac OS,
    // and a mailbox transaction repeated at each handover is one that has to
    // succeed every time. See okapia_output.h.
    TSurface Surface;
    if (!HostOutputClaim (&Surface))
    {
        HostLog (FROM, HostLogWarning, "No 32-bit output; going straight to the Mac");
        return FirmwareBoot;
    }
    const unsigned nWidth  = Surface.nWidth;
    const unsigned nHeight = Surface.nHeight;

    // The language before anything is drawn, since every label goes through it.
    // An unknown code answers the first language rather than failing: a card
    // written by a later version has to boot on an older firmware.
    StringsSetLanguage (StringsFromCode (PrefsFindString ("language")));

    TTheme Theme;
    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    ThemeMake (nScale16, &Theme);

    Theme.DrawDesktop (&Surface, &Theme);
    HostOutputPresent (0, 0, nWidth, nHeight);

    // The keyboard has been watched since USB came up, not since this function
    // started: a key held from power-on reports once, before any of this, and
    // says nothing more until it is released. Watching from here made Option
    // work when a machine sent it and never when a person held it.
    //
    // The devices are *not* given back afterwards. Circle keeps one handler
    // each and InputInit() puts the Macintosh's in their place when
    // StartMacintosh() runs, which is the whole handover. Registering 0 to
    // detach looks tidier and wedges the boot: it does not detach anything, it
    // puts the device back into cooked mode (usbkeyboard.cpp:200) and the
    // reports then go to CKeyboardBehaviour. Measured — with that call the
    // kernel stopped dead here with no further log at all.
    const bool bKeyboard = HostInputWatch ();
    HostInputBounds (nWidth, nHeight);

    // Read before the window rather than when the chooser opens, because
    // whether there is anything to start decides whether the window is the
    // right thing to show at all. It costs about a tenth of a second: the
    // inventory reads each image's master directory block, not its catalogue.
    s_Chooser.Built = Built == FirmwareEnginePowerPC ? CPUPowerPC : CPU68k;
    GatherVolumes (&s_Chooser);
    // Settles every volume's engine from its System and from what the card
    // remembers, so the answer below is right even when the window is never
    // opened — which is the ordinary boot. Not ChooserSync(): that speaks to
    // components, and none has been laid out at this point.
    ChooserSettleEngines (&s_Chooser);
    HostLog (FROM, HostLogNotice, "Chooser: %u volume(s), startup %d",
                            s_Chooser.nCount, s_Chooser.nStartup);

    // Asked for once and for all: the window is skipped entirely, not shortened.
    // Two seconds of nothing before a menu one has asked to see every time is
    // two seconds of a machine looking broken — and on a board with no keyboard
    // attached, Option is not an answer at all.
    const bool bAlways = PrefsFindBool ("bootmenu");

    HostLog (FROM, HostLogNotice,
                            "Output %ux%u, theme scale %u/16, language %s, %s, %s",
                            nWidth, nHeight, nScale16,
                            StringsCode (StringsLanguage ()),
                            bKeyboard ? "keyboard attached" : "no keyboard",
                            bAlways ? "boot menu asked for" : "holding for Option");

    // The window ends the moment it has its answer. Holding Option and then
    // watching two seconds of nothing is two seconds of a machine that looks
    // broken, and the wait was only ever there to give a hand time to arrive.
    //
    // Not the instant Option appears, though: Command-Option-P-R is four keys,
    // and a keyboard reports them as they are scanned rather than all at once.
    // Leaving on the first modifier would open the chooser at somebody who
    // asked to forget the parameter RAM. So Option starts a short grace instead
    // — long enough for the rest of a chord that is already held, far shorter
    // than the window it replaces.
    static const unsigned GRACE_MS = 250;
    unsigned nStop = WINDOW_MS;
    for (unsigned nWaited = 0; !bAlways && nWaited < nStop; nWaited += SLICE_MS)
    {
        // Drained rather than read, so that a window nobody touches cannot end
        // with a full queue: this one only wants the latch, but the queue is
        // the same one the screens will read from.
        TEvent Event;
        while (HostInputNext (&Event))
        {
        }
        HostPoll ();
        FwSleep (SLICE_MS);

        if (nStop == WINDOW_MS && (HostInputSeenModifiers () & ModOption))
        {
            const unsigned nEnd = nWaited + SLICE_MS + GRACE_MS;
            nStop = nEnd < WINDOW_MS ? nEnd : WINDOW_MS;
        }
    }

    const unsigned nSeen = HostInputSeenModifiers ();

    // Command-Option-P-R, the combination a Macintosh answered by forgetting its
    // parameter RAM. It is watched here rather than passed on, because a real
    // Macintosh does this in its ROM before the emulated ADB is alive at all:
    // letting it through would reach nobody.
    const bool bForgetPram = (nSeen & ModOption) && (nSeen & ModCommand)
                          && HostInputSeenKey (KEY_P) && HostInputSeenKey (KEY_R);

    // Option on its own opens the chooser. Every other combination including it
    // belongs to the Macintosh, so anything else held alongside disqualifies it.
    const bool bOption = !bForgetPram
                      && (nSeen & ModOption)
                      && !(nSeen & (ModCommand | ModControl | ModShift));

    // The preference is Option, standing. Not a third branch below: it has to
    // reach the same place by the same road, or the two ways in would drift.
    const bool bWanted = bOption || bAlways;

    if (bForgetPram)
    {
        // The Macintosh about to start, and only that one: a real Macintosh
        // forgot its own parameter RAM, and the other engine's is another
        // machine's. The boot menu's button can forget either, or both.
        const bool bPowerPC = FirmwareWantedEngine () == FirmwareEnginePowerPC;
        ForgetPram ("Command-Option-P-R", !bPowerPC, bPowerPC);
    }
    // Nothing to start from is not a state to hold a grey screen through and
    // then hand a question-mark floppy to: the firmware is the only thing that
    // can put it right, so it opens itself. This is the case a card written by
    // hand lands in, and the one where the machine looks broken and is not.
    else if (bWanted || s_Chooser.nStartup < 0)
    {
        HostLog (FROM, HostLogNotice,
                                bAlways ? "Boot menu asked for: the chooser"
                                        : bOption ? "Option held: the chooser"
                                                  : "Nothing to start: the chooser");
        const TFirmwareResult Chosen = RunScreens (&Surface, &Theme);
        HostLog (FROM, HostLogNotice, "Display handed back");
        return Chosen;
    }
    else if (HostInputSeenAnything ())
    {
        HostLog (FROM, HostLogNotice,
                                "Input during the window, none of ours: modifiers %02X",
                                nSeen);
    }

    HostLog (FROM, HostLogNotice, "Display handed back");
    return FirmwareBoot;
}
