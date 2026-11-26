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

#include "net_config_okapia.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Basilisk II's headers, and sysdeps.h first as everywhere in that tree. The
// firmware reads exactly one preference and would rather not drag the emulator
// in for it, but declaring PrefsFindString by hand is how a signature drifts.
#include "sysdeps.h"
#include "prefs.h"

#include "hfs_volume_okapia.h"

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
#include "okapia_rom.h"
#include "okapia_welcome.h"

#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>

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
// (xpram_okapia.cpp), and the firmware serves both, so it is told which.
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
// "builtin" is a desktop's: a board reading it plays on its jack (audio_okapia.cpp).
static const char *const SOUND_NAMES[SoundOutputCount] = { "off", "hdmi", "jack", "usb", "builtin" };

static void LoadSettings (TSettings *pSettings)
{
    // Snapped to what the screen can offer: a card naming a size the menu does
    // not have would otherwise be shown as the nearest one and saved as itself,
    // which is the interface lying about what the button will do.
    const int32 nRAM = PrefsFindInt32 ("ramsize");
    pSettings->V.nMemoryMB  = SettingsMemoryOffered ((unsigned) (nRAM / (1024 * 1024)));
    pSettings->V.nMouseDpi  = PrefsFindInt32 ("mousedpi");

    // Silence is what an unreadable value means, and nosound wins wherever the
    // two disagree — audio_okapia.cpp reads them the same way. A device claimed
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

    // Read at every start of the Macintosh (basilisk_glue.cpp), so it owes the
    // board no restart.
    pSettings->V.bJit = PrefsFindBool ("jit");
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
    if (HostKind () == HostDesktop)
    {
        // A desktop has one output; a card's HDMI or jack plays there too.
        pSettings->nSoundOffered = pSettings->nSoundAvailable;
        if (pSettings->V.nSound != SoundOff)
        {
            pSettings->V.nSound = SoundBuiltIn;
        }
    }

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
    PrefsReplaceString ("extfs", pSettings->V.bShared
                                 ? (pSettings->V.SharedPath[0] != '\0'
                                    ? pSettings->V.SharedPath : SHARED_PATH)
                                 : "");
    PrefsReplaceString ("extfsname", pSettings->V.SharedName);

    HostLog (FROM, HostLogNotice,
                            "Settings: %u MB, sound %s, language %s, %s, %s, "
                            "jit %s",
                            pSettings->V.nMemoryMB,
                            SOUND_NAMES[pSettings->V.nSound],
                            StringsCode ((TLanguage) pSettings->V.nLanguage),
                            pSettings->V.bShared ? pSettings->V.SharedPath
                                                 : "no shared folder",
                            pSettings->V.bBootMenu ? "boot menu always"
                                                   : "boot menu on Option",
                            pSettings->V.bJit ? "on" : "off");
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
        const char *pWhere = HostCardLocation ();
        if (pWhere != 0)
        {
            // A desktop's workspace: where it is, which is what one forgets,
            // and the room left on its disk.
            n = StrAppend (Line, sizeof Line, 0, pWhere);
            if (HostCardSpace (&nTotalMB, &nFreeMB))
            {
                n = StrAppend (Line, sizeof Line, n, " · ");
                n = StrAppendNumber (Line, sizeof Line, n, nFreeMB);
                n = StrAppend (Line, sizeof Line, n, " ");
                StrAppend (Line, sizeof Line, n, Str (StrMbFree));
            }
        }
        else if (HostCardSpace (&nTotalMB, &nFreeMB))
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
    InfoCredit (pInfo, "TeX Gyre Heros", "GUST Font License · GUST e-foundry");
    InfoCredit (pInfo, "stb_truetype", "Public domain · Sean Barrett");
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
// A page's Tick saw its model change shape (more lines, fewer): RunPage lays it
// out again, as for a resized window.
static bool s_bPageRelayoutAsked;

// A newer Okapia (HostUpdate, R5): the button at the top right of the boot
// menu's tab strip, polled while the tabbed pages run on a desktop. Failing
// that, a Pi card with an older Okapia than the release: Update the Card,
// which opens the welcome page on it, where the card is prepared.
static bool s_bMenuUpdates;
static char s_UpdateLabel[64];
static char s_UpdatePage[192];
static char s_UpdateNote[96];           // over the button, for the card
static bool s_bUpdateIsCard;            // the button is Update the Card
static int  s_nCardOutdated = -1;       // -1 not known yet, 0 no, 1 yes

static void Fill1 (char *pOut, unsigned nSize, TStringId Id, const char *pValue);

// Whether the workspace is a card Okapia was installed on, for a board, with
// an older release than the published one. The card is read once the release
// is known (its files hashed), and not again while the menu runs. Not asked
// when the person turned the check off: the welcome page still says it.
static bool MenuCardOutdated (const THostUpdate &U)
{
    if (s_nCardOutdated >= 0)
    {
        return s_nCardOutdated != 0;
    }
    THostCardPlan Plan;
    HostCardPlan (0, &Plan);            // the card's manifest only, while no release is known
    if (Plan.Board[0] == '\0' || U.State == HostUpdateOff)
    {
        s_nCardOutdated = 0;
        return false;
    }
    THostRelease R;
    HostRelease (&R);                   // the lookup starts now, in the background
    if (R.State == HostReleaseLooking)
    {
        return false;
    }
    if (R.State == HostReleaseReady)
    {
        HostCardPlan (0, &Plan);
    }
    s_nCardOutdated = Plan.bKnown && !Plan.bUpToDate && !Plan.bRefused ? 1 : 0;
    if (s_nCardOutdated != 0)
    {
        Fill1 (s_UpdateNote, sizeof s_UpdateNote, StrWelcomeRelUpdate, R.Version);
    }
    HostLog (FROM, HostLogNotice, "Card: Okapia %s for %s, release %s: %s", Plan.Installed, Plan.Board,
             R.State == HostReleaseReady ? R.Version : "unknown", s_nCardOutdated ? "older" : "kept");
    return s_nCardOutdated != 0;
}

// True when the strip's button came or went (the page is laid out again).
static bool MenuUpdatePoll (void)
{
    THostUpdate U;
    HostUpdate (&U);
    char Label[sizeof s_UpdateLabel] = "";
    s_bUpdateIsCard = false;
    if (U.State == HostUpdateAvailable)
    {
        Fill1 (Label, sizeof Label, StrUpdateButton, U.Version);
        snprintf (s_UpdatePage, sizeof s_UpdatePage, "%s", U.Page);
    }
    else if (U.State != HostUpdateChecking && MenuCardOutdated (U))
    {
        snprintf (Label, sizeof Label, "%s", Str (StrUpdateCardButton));
        s_bUpdateIsCard = true;
    }
    if (strcmp (Label, s_UpdateLabel) == 0)
    {
        return false;
    }
    snprintf (s_UpdateLabel, sizeof s_UpdateLabel, "%s", Label);
    PageChromeUpdate (s_UpdateLabel[0] != '\0' ? s_UpdateLabel : 0, s_bUpdateIsCard ? s_UpdateNote : 0);
    return true;
}
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
                    HostInputBounds (pOutput->nWidth, pOutput->nHeight);
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

        // A desktop window resized with the mouse: the page laid out again at
        // its new size, as a change of language does it.
        const bool bAsked = s_bPageRelayoutAsked;
        s_bPageRelayoutAsked = false;
        if (pDriver->Relayout != 0 && (HostOutputResized (pOutput) || bAsked))
        {
            pShadow->nWidth  = pOutput->nWidth;
            pShadow->nHeight = pOutput->nHeight;
            const int nFocus = Screen.nFocus;
            GfxCursorForget ();
            pDriver->Relayout (pShadow);
            nCount = pDriver->Widgets (&pWidgets);
            ScreenInit (&Screen, pTheme, pWidgets, nCount);
            Screen.Background = ColorWhite;
            Screen.Bounds     = Rect (0, 0, pOutput->nWidth, pOutput->nHeight);
            ScreenFocus (&Screen, nFocus);
            HostInputBounds (pOutput->nWidth, pOutput->nHeight);
            Screen.bDirtyAll = true;
            bChanged = true;
        }

        if (s_bMenuUpdates && nTick % 25 == 0 && pDriver->Relayout != 0 && MenuUpdatePoll ())
        {
            s_bPageRelayoutAsked = true;
        }

        if (pDriver->Tick != 0 && nTick % 25 == 0 && pDriver->Tick ())
        {
            Screen.bDirtyAll = true;
            bChanged = true;
        }

        // The pointer: the system's on a desktop, told only its shape, and a
        // move then redraws nothing; drawn into the output on a board.
        bool bDrawPointer = bPointer;
        if (bPointer)
        {
            HostInputPointer (&nX, &nY);
            bDrawPointer = !HostPointer (ScreenCursorAt (&Screen, nX, nY) == CursorBeam);
        }
        if (bChanged || (nTick % (500 / SLICE_MS) == 0 && ScreenBlinkCaret (&Screen))
            || bDrawPointer)
        {
            HostInputPointer (&nX, &nY);
            ScreenPresent (pShadow, pOutput, &Screen, pDriver->Repaint, bDrawPointer, nX, nY,
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

void FirmwareStartupRead (TFirmwareStartup *pOut)
{
    const char *pDisk  = PrefsFindString ("disk", 0);
    const char *pCdrom = PrefsFindString ("cdrom", 0);
    const char *pSound = PrefsFindString ("soundoutput");
    snprintf (pOut->Disk,  sizeof pOut->Disk,  "%s", pDisk  != 0 ? pDisk  : "");
    snprintf (pOut->Cdrom, sizeof pOut->Cdrom, "%s", pCdrom != 0 ? pCdrom : "");
    snprintf (pOut->Sound, sizeof pOut->Sound, "%s", pSound != 0 ? pSound : "");
    pOut->bNoSound    = PrefsFindBool ("nosound");
    pOut->nBootDriver = (int) PrefsFindInt32 ("bootdriver");
}

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
    s_Display.nDensity         = Facts.nDensity;
    s_Display.bWindowed        = HostKind () == HostDesktop;
    THostWindow Window;
    if (HostWindowDescribe (&Window))
    {
        s_Display.bWindowSizes = true;
        s_Display.nWindowMode  = (unsigned) Window.Mode;
        s_Display.Window = {Window.nWidth, Window.nHeight};
        // A desktop's menu draws in device pixels, its Macintosh in points:
        // the sizes offered to the Macintosh are the window's, in points.
        s_Display.Output = s_Display.Window;
        s_Display.Fill   = {Window.nFillWidth, Window.nFillHeight};
        s_Display.Screen = {Window.nScreenWidth, Window.nScreenHeight};
    }
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
static const int AnswerWindowSize    = 2002;
static const int AnswerWindowOther   = 2003;

static int DisplayAdapter (int nIndex)
{
    const int nChrome = PageChromeAnswer (nIndex);
    if (nChrome != PageAnswerNone)
        return nChrome;
    switch (DisplayOperate (&s_Display, nIndex, s_nCell))
    {
    case DisplayNewResolution:  return AnswerNewResolution;
    case DisplayWindowChosen:   return AnswerWindowSize;
    case DisplayWindowOther:    return AnswerWindowOther;
    default:                    return PageAnswerNone;
    }
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

static bool RunNewResolution (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                              bool bWindow = false)
{
    NewResolutionDraw (pShadow, &s_Display, bWindow);
    static const TPageDriver Driver =
    {
        NewResolutionRepaint, NewResolutionWidgets, NewResolutionAdapter,
        NewResolutionChanged, 0, NewResolutionCancelled
    };
    return RunPage (pOutput, pShadow, pTheme, &Driver) == (int) NewResolutionCreate;
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
    const bool bRestartWas = RestartWanted ();
    const TSettingsAction Action = SettingsOperate (&s_Settings, nIndex);
    if (s_Settings.bNetworkOffered)
    {
        // A desktop's network, chosen here: the wired kind is the host's.
        s_Network.V.nKind = s_Settings.nNetwork != 0 ? NetworkCable : NetworkOff;
        if (RestartWanted () != bRestartWas)
        {
            return PageRelayout;        // Start says Restart, or no longer
        }
    }
    switch (Action)
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

// The page the host started again for (HostMenuReopen), or -1.
static int s_nReopenTab = -1;

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
    s_Tab = s_nReopenTab >= 0 && s_nReopenTab < (int) PageTabCount ? (TPageTab) s_nReopenTab
                                                                       : PageTabSystem;
    s_bFocusTabs = false;
    // The way back to the welcome page, where there is more than one card.
    PageChromeWorkspace (HostHasWelcome ());
    // A desktop's network is one choice, the host's shared or none: it is on
    // the Settings page, and the Network page, the board's, is left out.
    const bool bDesktop = HostKind () == HostDesktop;
    PageChromeHideTab (PageTabNetwork, bDesktop);
    s_Settings.bNetworkOffered = bDesktop;
    s_Settings.nNetwork = s_Network.V.nKind != NetworkOff ? 1 : 0;
    s_bMenuUpdates = bDesktop;
    if (bDesktop)
    {
        MenuUpdatePoll ();              // the last answer, at once, when there is one
    }

    static const TPageDriver Drivers[PageTabCount] =
    {
        { ChooserRepaint,  ChooserWidgets,  ChooserAdapter,  ChooserChanged, RelayoutTab, 0, 0 },
        { SettingsRepaint, SettingsWidgets, SettingsAdapter, 0,              RelayoutTab, 0,
          SettingsTick },
        { NetworkRepaint,  NetworkWidgets,  NetworkAdapter,  0,              RelayoutTab, 0,
          NetworkTick },
        { DisplayRepaint,  DisplayWidgets,  DisplayAdapter,  0,              RelayoutTab, 0, 0 },
        { InfoRepaint,     InfoWidgets,     InfoAdapter,     0,              RelayoutTab, 0, 0 },
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

        case AnswerWindowOther:
            if (!RunNewResolution (pOutput, &Shadow, pTheme, true))
            {
                break;
            }
            // fall through: the size entered, applied as a listed one is
            [[fallthrough]];

        case AnswerWindowSize:
            // The output is the window's for the life of the process: what the
            // pages set is kept, and the host starts again at this page, at the
            // new size (a desktop's only; HostWindowApply).
            SaveAll ();
            HostLog (FROM, HostLogNotice, "Window: mode %u, %ux%u", s_Display.nWantedMode,
                     s_Display.Wanted.nWidth, s_Display.Wanted.nHeight);
            HostWindowApply ((THostWindowMode) s_Display.nWantedMode, s_Display.Wanted.nWidth,
                             s_Display.Wanted.nHeight, (int) PageTabDisplay);
            break;

        case AnswerClockHelp:
            Tell (pOutput, &Shadow, pTheme, StrClockGroup,
                  HostClockIsHosts () ? StrClockHelpDesktop : StrClockHelp);
            break;

        case PageAnswerBootMenu:
            s_Settings.V.bBootMenu = !s_Settings.V.bBootMenu;
            s_bFocusBootMenu = true;
            break;

        case PageAnswerUpdate:
            if (s_bUpdateIsCard)
            {
                // The welcome page, on this card, its board chosen: Update
                // the Card there, with its progress. What was set is kept.
                SaveAll ();
                HostOpenWelcome ();
            }
            else
            {
                HostOpenPage (s_UpdatePage);
            }
            break;

        case PageAnswerWorkspace:
            // Another workspace, or this one with more in it: what was set here
            // is kept first, as Shut Down keeps it.
            SaveAll ();
            HostOpenWelcome ();
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

/*
 *  The welcome page, on a desktop before any workspace
 *
 *  Runs before either engine, so before the preferences are read: what it
 *  changes in them it writes to the file itself, one line at a time, and the
 *  boot menu that follows reads the file as it always does.
 */
static const unsigned PATH_MAX_OKAPIA = 1024;
static TWelcome  s_Welcome;
static TSurface *s_pWelcomeOutput;
static TSurface *s_pWelcomeShadow;
static unsigned  s_nWelcomeMaxHeight;   // a resizable window's: the tallest the page may be

// The welcome page at the output's size. In a window the person resizes it is
// first laid out on the tallest output the host holds, to learn how much height
// it needs, and the output takes that (never less than the window); then it is
// laid out for real. The host keeps its title and its buttons in place and
// scrolls the steps between them, as the installer did.
static void WelcomeLayout (void)
{
    TSurface *pOut = s_pWelcomeOutput, *pShadow = s_pWelcomeShadow;
    if (s_Welcome.bFill)
    {
        pShadow->nWidth  = pOut->nWidth;
        pShadow->nHeight = s_nWelcomeMaxHeight;
        WelcomeDraw (pShadow, &s_Welcome);
        HostOutputExtend (pOut, s_nWelcomeMaxHeight - WelcomeSpare ());
        pShadow->nHeight = pOut->nHeight;
    }
    WelcomeDraw (pShadow, &s_Welcome);
    if (s_Welcome.bFill)
    {
        unsigned nTop = 0, nBottom = 0, nRight = 0;
        WelcomeBands (&nTop, &nBottom, &nRight);
        HostOutputBands (nTop, nBottom, nRight);
    }
}

// The SD card sheet over it: on exactly the window, at the top, so the sheet
// centred on the output is in the middle of what is seen.
static void WelcomeCardsLayout (void);
static const TTheme *s_pWelcomeTheme;

// A string with one "{}" in it, the value put there.
static void Fill1 (char *pOut, unsigned nSize, TStringId Id, const char *pValue)
{
    const char *pText = Str (Id);
    const char *pHole = strstr (pText, "{}");
    if (pHole == 0)
    {
        snprintf (pOut, nSize, "%s", pText);
        return;
    }
    snprintf (pOut, nSize, "%.*s%s%s", (int) (pHole - pText), pText, pValue, pHole + 2);
}

// "7.1 GB", "456 MB", the decimal point the language's own.
static void Bytes (char *pOut, unsigned nSize, unsigned long long nBytes)
{
    const unsigned long long nMB = nBytes / (1024ull * 1024);
    if (nMB >= 1024)
    {
        const unsigned long long nTenths = nMB * 10 / 1024;
        snprintf (pOut, nSize, "%llu%s%llu %s", nTenths / 10, Str (StrDecimalPoint), nTenths % 10,
                  Str (StrUnitGb));
    }
    else
    {
        snprintf (pOut, nSize, "%llu %s", nMB, Str (StrUnitMb));
    }
}

// What the workspace's volume is, for a Pi: what is wrong with it first, the
// summary when nothing is (the installer's card step, card_problem).
static void WelcomeVolumeNote (char *pOut, unsigned nSize, bool bBoard)
{
    pOut[0] = '\0';
    TCardVolume V;
    if (!HostVolumeOf (CardPath ("/"), &V))
    {
        return;
    }
    TCardDiagnosis D;
    CardDiagnose (CardPath ("/"), &V, &D);
    char Value[64];
    if (bBoard && V.bRemovable)
    {
        char Kb[16];
        snprintf (Kb, sizeof Kb, "%u", V.nCluster / 1024);
        if (V.Fs == CardFsFat12 || V.Fs == CardFsFat16)
        {
            Fill1 (pOut, nSize, StrWelcomeCNotFat32, V.Fs == CardFsFat12 ? "FAT12" : "FAT16");
            return;
        }
        if (V.Fs == CardFsOther)
        {
            Fill1 (pOut, nSize, StrWelcomeCNotFat, V.FsName);
            return;
        }
        if (D.bSmallClusters)
        {
            Fill1 (pOut, nSize, StrWelcomeCSmallClusters, Kb);
            return;
        }
        if (D.bUncleanShutdown)
        {
            snprintf (pOut, nSize, "%s", Str (StrWelcomeCUnclean));
            return;
        }
    }
    // The summary: what the volume is, its room, and Okapia on it.
    const TStringId What = V.bRemovable ? StrWelcomeCCard : bBoard ? StrWelcomeCStandIn : StrWelcomeCFolder;
    unsigned n = (unsigned) snprintf (pOut, nSize, "%s", Str (What));
    if (bBoard && V.bRemovable && V.Fs == CardFsFat32)
    {
        char Kb[16], Clusters[48];
        snprintf (Kb, sizeof Kb, "%u", V.nCluster / 1024);
        Fill1 (Clusters, sizeof Clusters, StrWelcomeCClusters, Kb);
        n += (unsigned) snprintf (pOut + n, n < nSize ? nSize - n : 0, " · FAT32, %s", Clusters);
    }
    Bytes (Value, sizeof Value, V.nFree);
    char Free[64];
    Fill1 (Free, sizeof Free, StrWelcomeCFree, Value);
    n += (unsigned) snprintf (pOut + n, n < nSize ? nSize - n : 0, " · %s", Free);
    if (bBoard)
    {
        char Okapia[96];
        if (D.Version[0] == '\0')
        {
            // Not in the kernel nor in a log: what the card's manifest says
            // was installed, when Okapia was installed there.
            THostCardPlan Plan;
            HostCardPlan (0, &Plan);
            snprintf (D.Version, sizeof D.Version, "%s", Plan.Installed);
        }
        if (D.bKernel && D.Missing[0] == '\0')
            Fill1 (Okapia, sizeof Okapia, StrWelcomeCOkapia, D.Version[0] != '\0' ? D.Version : "?");
        else if (D.bKernel)
            Fill1 (Okapia, sizeof Okapia, StrWelcomeCBootMissing, D.Missing);
        else
            snprintf (Okapia, sizeof Okapia, "%s", Str (StrWelcomeCNoOkapia));
        snprintf (pOut + n, n < nSize ? nSize - n : 0, " · %s", Okapia);
    }
}

/*
 *  The SD cards inserted: a sheet over the welcome page
 */
static TWelcomeCards s_Cards;
static TCardVolume   s_CardVolumes[WELCOME_CARDS];

// The cards as they are now; true when the list changed.
static bool CardsRead (void)
{
    TCardVolume Now[WELCOME_CARDS];
    const unsigned nNow = HostCards (Now, WELCOME_CARDS);
    bool bChanged = nNow != s_Cards.nCount;
    for (unsigned i = 0; i < nNow && !bChanged; i++)
    {
        bChanged = strcmp (Now[i].Mount, s_CardVolumes[i].Mount) != 0 || Now[i].nFree != s_CardVolumes[i].nFree;
    }
    if (!bChanged)
    {
        return false;
    }
    s_Cards.nCount = nNow;
    for (unsigned i = 0; i < nNow; i++)
    {
        s_CardVolumes[i] = Now[i];
        char Size[32];
        Bytes (Size, sizeof Size, Now[i].nTotal);
        const char *pFs = Now[i].Fs == CardFsFat32 ? "FAT32" : Now[i].Fs == CardFsFat16 ? "FAT16"
                        : Now[i].Fs == CardFsFat12 ? "FAT12" : Now[i].FsName;
        snprintf (s_Cards.Line[i], sizeof s_Cards.Line[i], "%s — %s, %s", Now[i].Name, pFs, Size);
    }
    return true;
}

static int s_nCardChosen = -1;

static int CardsAdapter (int nIndex)
{
    int nChosen = -1;
    switch (WelcomeCardsOperate (&s_Cards, nIndex, &nChosen))
    {
    case WelcomeCardsUse:
        s_nCardChosen = nChosen;
        return (int) WelcomeCardsUse;
    case WelcomeCardsCancel:
        return (int) WelcomeCardsCancel;
    default:
        return 0;
    }
}

static int CardsCancelled (void)
{
    return (int) WelcomeCardsCancel;
}

static bool CardsTick (void)
{
    if (!CardsRead ())
    {
        return false;
    }
    WelcomeCardsSync (&s_Cards);
    return true;
}

static void WelcomeCardsLayout (void)
{
    if (s_Welcome.bFill)
    {
        HostOutputExtend (s_pWelcomeOutput, 0);
        s_pWelcomeShadow->nWidth  = s_pWelcomeOutput->nWidth;
        s_pWelcomeShadow->nHeight = s_pWelcomeOutput->nHeight;
    }
    WelcomeCardsDraw (s_pWelcomeShadow, &s_Cards);
}

static void CardsRelayout (TSurface *)
{
    WelcomeCardsLayout ();
}

// The card chosen, its mount on the host; false when cancelled.
static bool RunCards (char *pMount, unsigned nSize)
{
    memset (&s_Cards, 0, sizeof s_Cards);
    s_Cards.nCount = ~0u;               // so the first read counts as a change
    CardsRead ();
    WelcomeCardsLayout ();
    static const TPageDriver Driver =
    {
        WelcomeCardsRepaint, WelcomeCardsWidgets, CardsAdapter, 0, CardsRelayout, CardsCancelled, CardsTick
    };
    s_nCardChosen = -1;
    const int nAnswer = RunPage (s_pWelcomeOutput, s_pWelcomeShadow, s_pWelcomeTheme, &Driver);
    WelcomeLayout ();
    if (nAnswer != (int) WelcomeCardsUse || s_nCardChosen < 0 || s_nCardChosen >= (int) s_Cards.nCount)
    {
        return false;
    }
    snprintf (pMount, nSize, "%s", s_CardVolumes[s_nCardChosen].Mount);
    return true;
}

// Set "<name> <value>", in place of every line of that name, or added to them
// when bAdd (disk, cdrom: an item that may come more than once, and this value
// is then added only once). Written beside and renamed over, so an abrupt stop
// leaves the old file or the new one, never half of one.
static bool PrefsFileSet (const char *pName, const char *pValue, bool bAdd)
{
    // A save of the engine's that stopped half-way leaves the only good copy
    // in Okapia_Prefs.new: finished first, or this would rewrite it as one line.
    CardPrefsRecover ();
    static char Old[64 * 1024];
    size_t nOld = 0;
    FILE *pIn = fopen (CardPath ("/Okapia_Prefs"), "r");
    if (pIn != 0)
    {
        nOld = fread (Old, 1, sizeof Old - 1, pIn);
        fclose (pIn);
    }
    Old[nOld] = '\0';

    char Temp[] = "/Okapia_Prefs.new";
    FILE *pOut = fopen (CardPath (Temp), "w");
    if (pOut == 0)
    {
        return false;
    }
    const size_t nName = strlen (pName);
    bool bThere = false;
    for (char *pLine = Old; *pLine != '\0'; )
    {
        char *pEnd = strchr (pLine, '\n');
        const size_t nLine = pEnd != 0 ? (size_t) (pEnd - pLine) : strlen (pLine);
        const bool bSame = nLine > nName && strncmp (pLine, pName, nName) == 0 && pLine[nName] == ' ';
        if (bSame && bAdd && nLine == nName + 1 + strlen (pValue)
            && strncmp (pLine + nName + 1, pValue, nLine - nName - 1) == 0)
        {
            bThere = true;
        }
        if (!bSame || bAdd)
        {
            fwrite (pLine, 1, nLine, pOut);
            fputc ('\n', pOut);
        }
        pLine += nLine + (pEnd != 0 ? 1 : 0);
    }
    if (!bThere)
    {
        fprintf (pOut, "%s %s\n", pName, pValue);
    }
    const bool bWritten = fflush (pOut) == 0 && fsync (fileno (pOut)) == 0;
    fclose (pOut);
    char Final[PATH_MAX_OKAPIA];
    snprintf (Final, sizeof Final, "%s", CardPath ("/Okapia_Prefs"));
    const bool bRenamed = bWritten && rename (CardPath (Temp), Final) == 0;
    HostTidy ("/Okapia_Prefs.new");
    HostTidy ("/Okapia_Prefs");
    return bRenamed;
}

static bool PrefsFileGet (const char *pName, char *pOut, unsigned nSize);

// What the workspace holds, read again after every change.
static void WelcomeGather (void)
{
    TWelcome &W = s_Welcome;
    W.nSystems = 0;
    const char *pWhere = HostCardLocation ();
    // The end of the path is what tells one workspace from another, so a long
    // one loses its beginning, not its name.
    const char *pShown = pWhere != 0 ? pWhere : "";
    const size_t nShown = strlen (pShown);
    if (nShown > 64)
    {
        pShown += nShown - 63;
        while ((*pShown & 0xC0) == 0x80)
        {
            pShown++;                   // not in the middle of a character
        }
        snprintf (W.Workspace, sizeof W.Workspace, "…%s", pShown);
    }
    else
    {
        snprintf (W.Workspace, sizeof W.Workspace, "%s", pShown);
    }
    W.WorkspaceNote[0] = '\0';
    if (W.Workspace[0] != '\0')
    {
        WelcomeVolumeNote (W.WorkspaceNote, sizeof W.WorkspaceNote, W.nTarget != WelcomeForDesktop);
    }
    if (W.Workspace[0] == '\0')
    {
        // No workspace yet, and still which ROMs to look for.
        for (unsigned e = 0; e < WelcomeRomCount; e++)
        {
            memset (&W.Rom[e], 0, sizeof W.Rom[e]);
            snprintf (W.Rom[e].Line, sizeof W.Rom[e].Line, "%s", Str (StrWelcomeRomNone));
            snprintf (W.Rom[e].Note, sizeof W.Rom[e].Note, "%s",
                      Str (e == WelcomeRom68k ? StrWelcomeRomNone68k : StrWelcomeRomNonePpc));
        }
        W.bNothingStarts = false;
        return;
    }

    // The ROMs: for each engine, the one the preferences name, else the first
    // usable one of its family, as the installer chose (card_rom).
    char Named[WelcomeRomCount][64];
    PrefsFileGet ("rom", Named[WelcomeRom68k], sizeof Named[0]);
    PrefsFileGet ("romppc", Named[WelcomeRomPowerPC], sizeof Named[0]);
    TRomFacts Chosen[WelcomeRomCount];
    char ChosenCard[WelcomeRomCount][64] = { "", "" };
    DIR *pDir = opendir (CardPath ("/"));
    if (pDir != 0)
    {
        struct dirent *pEntry;
        while ((pEntry = readdir (pDir)) != 0)
        {
            if (pEntry->d_name[0] == '.')
            {
                continue;
            }
            char Card[64];
            if (snprintf (Card, sizeof Card, "/%s", pEntry->d_name) >= (int) sizeof Card)
            {
                continue;
            }
            struct stat Info;
            if (stat (CardPath (Card), &Info) != 0 || !S_ISREG (Info.st_mode)
                || !RomLooksLikeOne (pEntry->d_name, (unsigned long) Info.st_size))
            {
                continue;
            }
            TRomFacts Facts;
            if (!RomIdentify (CardPath (Card), &Facts) || Facts.Verdict != RomUsable)
            {
                continue;
            }
            const unsigned e = Facts.Kind == Rom68k ? WelcomeRom68k : WelcomeRomPowerPC;
            const bool bNamed = strcmp (Named[e], Card) == 0;
            if (ChosenCard[e][0] == '\0' || (bNamed && strcmp (Named[e], ChosenCard[e]) != 0))
            {
                Chosen[e] = Facts;
                snprintf (ChosenCard[e], sizeof ChosenCard[e], "%s", Card);
            }
        }
        closedir (pDir);
    }
    for (unsigned e = 0; e < WelcomeRomCount; e++)
    {
        TWelcomeRom &R = W.Rom[e];
        memset (&R, 0, sizeof R);
        if (ChosenCard[e][0] == '\0')
        {
            snprintf (R.Line, sizeof R.Line, "%s", Str (StrWelcomeRomNone));
            snprintf (R.Note, sizeof R.Note, "%s",
                      Str (e == WelcomeRom68k ? StrWelcomeRomNone68k : StrWelcomeRomNonePpc));
            continue;
        }
        R.bThere = true;
        snprintf (R.CardPath, sizeof R.CardPath, "%s", ChosenCard[e]);
        snprintf (R.Line, sizeof R.Line, "%s · %s", Chosen[e].Identity, ChosenCard[e] + 1);
        snprintf (R.Note, sizeof R.Note, "%s",
                  Str (e == WelcomeRom68k ? (Chosen[e].bRecommended ? StrWelcomeRomStarts68k
                                                                     : StrWelcomeRomNotQuadra)
                       : Chosen[e].nFirstSystem >= 810 ? StrWelcomeRomStartsNew
                                                       : StrWelcomeRomStartsOld));
        R.bChime = HostChimeKnown (ChosenCard[e]);
    }

    // The disks, each with what the ROMs at hand make of it.
    const TRomFacts *p68k = W.Rom[WelcomeRom68k].bThere ? &Chosen[WelcomeRom68k] : 0;
    const TRomFacts *pPpc = W.Rom[WelcomeRomPowerPC].bThere ? &Chosen[WelcomeRomPowerPC] : 0;
    W.bNothingStarts = false;
    bool bAnyStarts = false;
    static THfsVolumeInfo Found[CHOOSER_MAX];
    const unsigned nFound = HfsInventory (Found, CHOOSER_MAX);
    for (unsigned i = 0; i < nFound; i++)
    {
        THfsSystemVersion Version;
        memset (&Version, 0, sizeof Version);
        const bool bBlessed = Found[i].Blessed != 0;
        const bool bVersion = bBlessed && HfsSystemVersion (Found[i].Path, &Version);
        const unsigned nCode = bVersion ? Version.nMajor * 100 + Version.nMinor * 10 + Version.nBugfix : 0;
        TDiskFlavour Flavour = FlavourUnreadable;
        if (bVersion)
        {
            switch (HfsFlavourOf (&Version))
            {
            case HfsFlavour68k:       Flavour = Flavour68k;       break;
            case HfsFlavourPowerPC:   Flavour = FlavourPowerPC;   break;
            case HfsFlavourUniversal: Flavour = FlavourUniversal; break;
            default:                  break;
            }
        }
        const TDiskJudgement J = DiskJudge (true, bBlessed, nCode, Flavour, p68k, pPpc);
        bAnyStarts = bAnyStarts || DiskJudgementStarts (J);
        if (W.nSystems < WELCOME_LINES)
        {
            static const TStringId Verdicts[] =
            {
                StrWelcomeVData, StrWelcomeVNoSystem, StrWelcomeVUnreadable, StrWelcomeVTooNew,
                StrWelcomeVStarts68k, StrWelcomeVStartsPpc, StrWelcomeVStartsBoth,
                StrWelcomeVNeeds68k, StrWelcomeVNeedsPpc, StrWelcomeVNeedsAny, StrWelcomeVPpcFloor
            };
            char System[48] = "";
            if (bVersion)
            {
                const TStringId Fl = Flavour == FlavourPowerPC ? StrWelcomeFlPpc
                                   : Flavour == FlavourUniversal ? StrWelcomeFlUniversal : StrWelcomeFl68k;
                char Number[16];
                snprintf (Number, sizeof Number, Version.nBugfix != 0 ? "%u.%u.%u" : "%u.%u",
                          Version.nMajor, Version.nMinor, Version.nBugfix);
                snprintf (System, sizeof System, " — %s %s, %s", nCode >= 760 ? "Mac OS" : "System",
                          Number, Str (Fl));
            }
            snprintf (W.System[W.nSystems], WELCOME_LINE, "%s%s · %s", Found[i].Name, System,
                      Str (Verdicts[J.Verdict]));
        }
        W.nSystems++;
    }
    W.bNothingStarts = nFound != 0 && !bAnyStarts;
}

// One line of the preferences file, its value after the name: what the
// welcome page reads before any engine has read the file.
static bool PrefsFileGet (const char *pName, char *pOut, unsigned nSize)
{
    CardPrefsRecover ();
    pOut[0] = '\0';
    FILE *pIn = fopen (CardPath ("/Okapia_Prefs"), "r");
    if (pIn == 0)
    {
        return false;
    }
    char Line[512];
    const size_t nName = strlen (pName);
    bool bFound = false;
    while (!bFound && fgets (Line, sizeof Line, pIn) != 0)
    {
        if (strncmp (Line, pName, nName) == 0 && Line[nName] == ' ')
        {
            snprintf (pOut, nSize, "%s", Line + nName + 1);
            pOut[strcspn (pOut, "\r\n")] = '\0';
            bFound = true;
        }
    }
    fclose (pIn);
    return bFound;
}

static void WelcomeStatus (TStringId What, const char *pName)
{
    snprintf (s_Welcome.Status, sizeof s_Welcome.Status, "%s %s", Str (What), pName);
}

// The status line on the screen now, before a copy that may take a while.
static void WelcomeShowNow (void)
{
    WelcomeSync (&s_Welcome);
    WelcomeRepaint (s_pWelcomeShadow);
    const TRect Whole = Rect (0, 0, s_pWelcomeOutput->nWidth, s_pWelcomeOutput->nHeight);
    GfxBlit (s_pWelcomeOutput, s_pWelcomeShadow, Whole);
    HostOutputPresent (0, 0, s_pWelcomeOutput->nWidth, s_pWelcomeOutput->nHeight);
}

// A folder with nothing in it but what Finder leaves (.DS_Store).
static bool FolderEmpty (const char *pPath)
{
    DIR *pDir = opendir (pPath);
    if (pDir == 0)
    {
        return false;
    }
    bool bEmpty = true;
    while (const struct dirent *p = readdir (pDir))
    {
        if (strcmp (p->d_name, ".") != 0 && strcmp (p->d_name, "..") != 0 && strcmp (p->d_name, ".DS_Store") != 0)
        {
            bEmpty = false;
            break;
        }
    }
    closedir (pDir);
    return bEmpty;
}

static const char *BaseName (const char *pPath)
{
    const char *pSlash = strrchr (pPath, '/');
    return pSlash != 0 ? pSlash + 1 : pPath;
}

static bool IsCdImage (const char *pName)
{
    const char *pDot = strrchr (pName, '.');
    return pDot != 0 && (strcasecmp (pDot, ".iso") == 0 || strcasecmp (pDot, ".toast") == 0
                         || strcasecmp (pDot, ".cdr") == 0);
}

// Copied in, then named in the preferences: a ROM by the engine it is for, a
// disk as a disk, a CD image as a CD.
static void WelcomeAdd (bool bRom)
{
    char Host[PATH_MAX_OKAPIA];
    if (!HostPick (bRom ? HostPickRom : HostPickDisk,
                   Str (bRom ? StrWelcomePickRom : StrWelcomePickSystem), Host, sizeof Host))
    {
        return;
    }
    const char *pName = BaseName (Host);
    if (bRom)
    {
        TRomFacts Facts;
        if (!RomIdentify (Host, &Facts) || Facts.Verdict != RomUsable)
        {
            const TStringId Why = Facts.Verdict == RomNot32BitClean    ? StrWelcomeRomBad32bit
                                : Facts.Verdict == RomChecksumMismatch ? StrWelcomeRomBadChecksum
                                : Facts.Verdict == RomUnknownNanokernel ? StrWelcomeRomBadNanokernel
                                                                       : StrWelcomeRomBadUnknown;
            snprintf (s_Welcome.Status, sizeof s_Welcome.Status, "%s %s (%s)",
                      Str (StrWelcomeNotAdded), pName, Str (Why));
            return;
        }
    }
    WelcomeStatus (StrWelcomeCopying, pName);
    WelcomeShowNow ();

    char Card[64];
    if (!HostCopyToCard (Host, Card, sizeof Card))
    {
        WelcomeStatus (StrWelcomeNotAdded, pName);
        return;
    }
    HostTidy (Card);
    bool bNamed = true;
    if (bRom)
    {
        TRomFacts Facts;
        RomIdentify (CardPath (Card), &Facts);
        bNamed = PrefsFileSet (Facts.Kind == Rom68k ? "rom" : "romppc", Card, false);
    }
    else
    {
        bNamed = PrefsFileSet (IsCdImage (Card) ? "cdrom" : "disk", Card, true);
    }
    WelcomeStatus (bNamed ? StrWelcomeAdded : StrWelcomeNotAdded, Card + 1);
    HostLog (FROM, bNamed ? HostLogNotice : HostLogWarning, "Welcome: %s %s",
             bNamed ? "added" : "could not name", Card);
}

/*
 *  The release a card is prepared with (HostRelease, HostPrepare*)
 */
static THostReleaseState s_ReleaseSeen = (THostReleaseState) -1;
static bool              s_bPreparing;
static THostPrepareState s_PrepareSeen;
static bool              s_bStatusIsOurs;   // the status line says the release's state, not an action's

// The For menu: this computer, then the release's boards, "Raspberry Pi 4"
// until a manifest is read. The board chosen stays chosen as the list grows.
static void WelcomeTargets (const THostRelease &R)
{
    TWelcome &W = s_Welcome;
    char Was[16];
    snprintf (Was, sizeof Was, "%s", W.nTarget < W.nTargets ? W.TargetId[W.nTarget] : "");
    W.nTargets = 0;
    snprintf (W.TargetName[0], sizeof W.TargetName[0], "%s", Str (StrWelcomeTargetDesktop));
    W.TargetId[0][0] = '\0';
    W.nTargets = 1;
    if (R.State == HostReleaseReady && R.nBoards != 0)
    {
        for (unsigned i = 0; i < R.nBoards && W.nTargets < WELCOME_TARGETS; i++, W.nTargets++)
        {
            snprintf (W.TargetName[W.nTargets], sizeof W.TargetName[0], "%s", R.BoardName[i]);
            snprintf (W.TargetId[W.nTargets], sizeof W.TargetId[0], "%s", R.BoardId[i]);
        }
    }
    else
    {
        snprintf (W.TargetName[1], sizeof W.TargetName[0], "%s", Str (StrWelcomeTargetRpi4));
        snprintf (W.TargetId[1], sizeof W.TargetId[0], "pi4");
        W.nTargets = 2;
    }
    if (Was[0] != '\0')
    {
        W.nTarget = WelcomeForFirstBoard;
        for (unsigned t = 1; t < W.nTargets; t++)
            if (strcmp (W.TargetId[t], Was) == 0)
                W.nTarget = t;
    }
    else if (W.nTarget >= W.nTargets)
    {
        W.nTarget = WelcomeForDesktop;
    }
}

// The Prepare button, and the status line while nothing else speaks there.
static void WelcomeReleaseState (void)
{
    TWelcome &W = s_Welcome;
    THostRelease R;
    HostRelease (&R);
    const bool bBoard = W.nTarget != WelcomeForDesktop && W.nTarget < W.nTargets;
    W.bPrepareEnabled = false;
    W.bUpdate = false;
    char Line[sizeof W.Status] = "";
    if (bBoard && W.Workspace[0] != '\0')
    {
        THostCardPlan Plan;
        HostCardPlan (W.TargetId[W.nTarget], &Plan);
        W.bUpdate = Plan.bKnown && Plan.Installed[0] != '\0' && !Plan.bUpToDate;
        W.bPrepareEnabled = R.State == HostReleaseReady && Plan.bKnown && !Plan.bUpToDate && !s_bPreparing;
        if (R.State == HostReleaseReady && Plan.bRefused)
            Fill1 (Line, sizeof Line, StrWelcomeRelRefused, R.Version);
        else if (R.State == HostReleaseReady && Plan.bKnown)
            Fill1 (Line, sizeof Line, Plan.bUpToDate ? StrWelcomeRelUptodate
                                      : W.bUpdate ? StrWelcomeRelUpdate : StrWelcomeRelReady, R.Version);
    }
    if (bBoard && Line[0] == '\0')
    {
        switch (R.State)
        {
        case HostReleaseLooking: snprintf (Line, sizeof Line, "%s", Str (StrWelcomeRelLooking)); break;
        case HostReleaseNone:    snprintf (Line, sizeof Line, "%s", Str (StrWelcomeRelNone));    break;
        case HostReleaseFailed:  Fill1 (Line, sizeof Line, StrWelcomeRelFailed, R.Why);         break;
        default:                 break;
        }
    }
    if (Line[0] != '\0' && !s_bPreparing && (W.Status[0] == '\0' || s_bStatusIsOurs))
    {
        snprintf (W.Status, sizeof W.Status, "%s", Line);
        s_bStatusIsOurs = true;
    }
}

// Four times a second: the release as it arrives, the card as it is written.
// What the workspace's top level holds, in one number: names, sizes and times.
// Files copied in by hand show up without a button.
static unsigned long WelcomeFolderSignature (void)
{
    unsigned long nSum = 5381;
    DIR *pDir = opendir (CardPath ("/"));
    if (pDir == 0)
    {
        return 0;
    }
    while (const struct dirent *p = readdir (pDir))
    {
        if (p->d_name[0] == '.')
        {
            continue;
        }
        char Card[80];
        if (snprintf (Card, sizeof Card, "/%s", p->d_name) >= (int) sizeof Card)
        {
            continue;
        }
        struct stat Info;
        unsigned long nOne = 0;
        for (const char *c = p->d_name; *c != '\0'; c++)
        {
            nOne = nOne * 33 + (unsigned char) *c;
        }
        if (stat (CardPath (Card), &Info) == 0)
        {
            nOne = nOne * 31 + (unsigned long) Info.st_size;
            nOne = nOne * 31 + (unsigned long) Info.st_mtime;
        }
        nSum += nOne;                   // the order readdir gives does not matter
    }
    closedir (pDir);
    return nSum;
}

static unsigned long s_nFolderSeen;
static unsigned      s_nFolderTicks;

static bool WelcomeTick (void)
{
    bool bChanged = false;
    // A newer Okapia, as the check answers (at once from the last one).
    {
        THostUpdate U;
        HostUpdate (&U);
        char Line[sizeof s_Welcome.Update] = "";
        if (U.State == HostUpdateAvailable)
        {
            Fill1 (Line, sizeof Line, StrUpdateButton, U.Version);
            snprintf (s_UpdatePage, sizeof s_UpdatePage, "%s", U.Page);
        }
        if (strcmp (Line, s_Welcome.Update) != 0)
        {
            snprintf (s_Welcome.Update, sizeof s_Welcome.Update, "%s", Line);
            s_bPageRelayoutAsked = true;
            bChanged = true;
        }
    }
    // Every two seconds, the folder read again; a change is gathered and laid
    // out, the disks' lines being as many as there are disks.
    if (s_Welcome.Workspace[0] != '\0' && !s_bPreparing && ++s_nFolderTicks % 8 == 0)
    {
        const unsigned long nNow = WelcomeFolderSignature ();
        if (nNow != s_nFolderSeen)
        {
            s_nFolderSeen = nNow;
            WelcomeGather ();
            s_bPageRelayoutAsked = true;
            bChanged = true;
        }
    }
    THostRelease R;
    HostRelease (&R);
    if (R.State != s_ReleaseSeen)
    {
        s_ReleaseSeen = R.State;
        WelcomeTargets (R);
        bChanged = true;
    }
    if (s_bPreparing)
    {
        THostPrepare P;
        HostPrepareStatus (&P);
        char Name[160];
        if (P.State == HostPrepareFetching || P.State == HostPrepareWriting)
        {
            snprintf (Name, sizeof Name, "%s… %u %%", P.Name, P.nPercent);
            Fill1 (s_Welcome.Status, sizeof s_Welcome.Status,
                   P.State == HostPrepareFetching ? StrWelcomePrepFetching : StrWelcomePrepWriting, Name);
            bChanged = true;
        }
        else if (P.State == HostPrepareDone || P.State == HostPrepareFailed)
        {
            s_bPreparing = false;
            if (P.State == HostPrepareDone)
                snprintf (s_Welcome.Status, sizeof s_Welcome.Status, "%s", Str (StrWelcomePrepDone));
            else
                Fill1 (s_Welcome.Status, sizeof s_Welcome.Status, StrWelcomePrepFailed, P.Why);
            s_bStatusIsOurs = false;
            WelcomeGather ();           // the card's note: Okapia on it now
            bChanged = true;
        }
    }
    if (bChanged)
    {
        WelcomeReleaseState ();
        WelcomeSync (&s_Welcome);
    }
    return bChanged;
}

// A card in a reader is for a board: the "For" menu follows it, as the
// installer's did, and so does a folder Okapia was installed on (a card's
// copy, the boot menu's Update the Card); any other folder leaves the choice
// as it was.
static void WelcomeTargetFromVolume (void)
{
    if (s_Welcome.Workspace[0] == '\0')
    {
        return;
    }
    TCardVolume V;
    THostCardPlan Plan;
    HostCardPlan (0, &Plan);
    if (Plan.Board[0] != '\0' || (HostVolumeOf (CardPath ("/"), &V) && V.bRemovable))
    {
        s_Welcome.nTarget = WelcomeForFirstBoard;
        for (unsigned t = 1; t < s_Welcome.nTargets; t++)
            if (strcmp (s_Welcome.TargetId[t], Plan.Board) == 0)
                s_Welcome.nTarget = t;
    }
}

static void WelcomeUse (const char *pHost)
{
    if (HostWorkspaceSet (pHost))
    {
        s_Welcome.Status[0] = '\0';
        snprintf (s_Welcome.Workspace, sizeof s_Welcome.Workspace, "%s", pHost);
        WelcomeTargetFromVolume ();
        HostLog (FROM, HostLogNotice, "Welcome: workspace %s", pHost);
    }
    else
    {
        WelcomeStatus (StrWelcomeNotAdded, pHost);
    }
}

static int WelcomeAdapter (int nIndex)
{
    char Host[PATH_MAX_OKAPIA];
    switch (WelcomeOperate (&s_Welcome, nIndex))
    {
    case WelcomeTarget:
        s_Welcome.Status[0] = '\0';     // the hint for the other target goes too
        s_bStatusIsOurs = false;
        break;

    case WelcomePrepare:
        if (s_Welcome.nTarget != WelcomeForDesktop && HostPrepareStart (s_Welcome.TargetId[s_Welcome.nTarget]))
        {
            s_bPreparing = true;
            s_bStatusIsOurs = false;
            s_Welcome.bPrepareEnabled = false;
            WelcomeSync (&s_Welcome);
        }
        return 0;

    case WelcomeCard:
        if (RunCards (Host, sizeof Host))
        {
            WelcomeUse (Host);
        }
        break;

    case WelcomeOpen:
        if (HostPick (HostPickWorkspace, Str (StrWelcomePickOpen), Host, sizeof Host))
        {
            WelcomeUse (Host);
        }
        break;

    case WelcomeCreate:
        // A save panel's answer: the new folder's path, named by the person
        // (the proposed name comes in Host). Made here; an empty folder of that
        // name will do, one that holds something never does.
        snprintf (Host, sizeof Host, "%s", Str (StrWelcomeNewName));
        if (HostPick (HostPickWorkspaceNew, Str (StrWelcomePickCreate), Host, sizeof Host))
        {
            if (mkdir (Host, 0755) != 0 && !(errno == EEXIST && FolderEmpty (Host)))
            {
                Fill1 (s_Welcome.Status, sizeof s_Welcome.Status, StrWelcomeNotCreated, BaseName (Host));
                s_bStatusIsOurs = false;
                break;
            }
            WelcomeUse (Host);
            // The language the welcome page spoke, which the boot menu then
            // speaks too.
            const char *pCode = HostLanguage ();
            if (pCode != 0 && StringsFromCode (pCode) != (TLanguage) 0)
            {
                PrefsFileSet ("language", pCode, false);
            }
        }
        break;

    case WelcomeAddRom:
        WelcomeAdd (true);
        break;

    case WelcomeListen68k:
    case WelcomeListenPowerPC:
    {
        // Played as the board plays it; the page does not change.
        const TWelcomeRom &R = s_Welcome.Rom[WelcomeOperate (&s_Welcome, nIndex) == WelcomeListen68k
                                             ? WelcomeRom68k : WelcomeRomPowerPC];
        if (R.bThere)
        {
            HostChimeListen (R.CardPath);
        }
        return 0;
    }

    case WelcomeAddSystem:
        WelcomeAdd (false);
        break;

    case WelcomeContinue:
        return (int) WelcomeContinue;

    case WelcomeQuit:
        return (int) WelcomeQuit;

    case WelcomeUpdate:
        HostOpenPage (s_UpdatePage);
        return 0;

    default:
        return 0;
    }
    WelcomeGather ();
    WelcomeReleaseState ();
    return PageRelayout;
}

static void WelcomeRelayout (TSurface *)
{
    WelcomeLayout ();
}

bool FirmwareWelcome (void)
{
    TSurface Surface;
    if (!HostOutputClaim (&Surface))
    {
        return false;
    }
    HostInputWatch ();
    HostInputBounds (Surface.nWidth, Surface.nHeight);
    StringsSetLanguage (StringsFromCode (HostLanguage ()));
    TTheme Theme;
    const unsigned nScale16 = ThemeScaleFor (Surface.nWidth, Surface.nHeight);
    ThemeMake (nScale16, &Theme);

    // A window the person resizes: the page fills it at the scale it opened
    // with, on a shadow as large as the output may grow.
    unsigned nMaxW = Surface.nWidth, nMaxH = Surface.nHeight;
    const bool bFill = HostOutputResizable (&nMaxW, &nMaxH);
    if (bFill)
    {
        PageFixScale (nScale16);
    }
    s_nWelcomeMaxHeight = nMaxH;
    TSurface Shadow = Surface;
    Shadow.nPitch  = nMaxW * (unsigned) sizeof (unsigned);
    Shadow.pPixels = new unsigned char[(size_t) Shadow.nPitch * nMaxH];
    s_pWelcomeOutput = &Surface;
    s_pWelcomeShadow = &Shadow;
    s_pWelcomeTheme  = &Theme;

    memset (&s_Welcome, 0, sizeof s_Welcome);
    s_Welcome.bFill = bFill;
    s_ReleaseSeen = (THostReleaseState) -1;
    s_bPreparing = false;
    s_bStatusIsOurs = false;
    THostRelease Release;
    HostRelease (&Release);             // the lookup starts now, in the background
    WelcomeTargets (Release);
    WelcomeGather ();
    WelcomeTargetFromVolume ();         // opened on a card: for the Pi
    WelcomeGather ();
    WelcomeReleaseState ();
    WelcomeLayout ();
    HostInputBounds (Surface.nWidth, Surface.nHeight);

    static const TPageDriver Driver =
    {
        WelcomeRepaint, WelcomeWidgets, WelcomeAdapter, 0, WelcomeRelayout, 0, WelcomeTick
    };
    const int nAnswer = RunPage (&Surface, &Shadow, &Theme, &Driver);
    delete[] Shadow.pPixels;
    PageFixScale (0);
    if (nAnswer != (int) WelcomeContinue || s_Welcome.Workspace[0] == '\0')
    {
        return false;
    }
    // A ROM copied in by hand is shown on the page; the engine reads only the
    // one the preferences name, so the page's choice is written down.
    static const char *const Keys[WelcomeRomCount] = { "rom", "romppc" };
    for (unsigned e = 0; e < WelcomeRomCount; e++)
    {
        char Named[64] = "";
        PrefsFileGet (Keys[e], Named, sizeof Named);
        if (s_Welcome.Rom[e].bThere && strcmp (Named, s_Welcome.Rom[e].CardPath) != 0)
        {
            PrefsFileSet (Keys[e], s_Welcome.Rom[e].CardPath, false);
            HostLog (FROM, HostLogNotice, "Welcome: %s %s, found in the workspace", Keys[e],
                     s_Welcome.Rom[e].CardPath);
        }
    }
    HostWorkspaceStart ();
    return true;
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
    unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    // A desktop's output is in device pixels, its window in points: the scale
    // the window's points give, times their density, so a 5K screen's menu is
    // the size a 1080p one's is, only sharper (ThemeScaleFor stops at four
    // times, which pixels reach long before points do). Every page takes it.
    THostWindow Window;
    if (HostWindowDescribe (&Window) && Window.nWidth != 0 && nWidth > Window.nWidth)
    {
        nScale16 = ThemeScaleFor (Window.nWidth, Window.nHeight) * nWidth / Window.nWidth;
    }
    PageFixScale (HostKind () == HostDesktop ? nScale16 : 0);
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
    // Not when the host has just started itself again on an answer the menu
    // gave (HostMenuAnswered): asking the same question twice reads as a loop.
    const bool bAnswered = HostMenuAnswered ();
    // Started again for the menu itself (a new window size): straight to it.
    s_nReopenTab = HostMenuReopen ();
    const bool bAlways = (PrefsFindBool ("bootmenu") || s_nReopenTab >= 0) && !bAnswered;

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
    for (unsigned nWaited = 0; !bAlways && !bAnswered && nWaited < nStop; nWaited += SLICE_MS)
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
