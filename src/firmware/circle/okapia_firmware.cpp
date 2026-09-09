//
// okapia_firmware.cpp — the boot firmware, as the kernel sees it.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_firmware.h"

#include <circle/bcmframebuffer.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <wrap_fatfs.h>

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
#include "okapia_info.h"
#include "okapia_newvolume.h"
#include "okapia_settings.h"
#include "okapia_gfx.h"
#include "okapia_screen.h"
#include "okapia_strings.h"
#include "okapia_input.h"
#include "okapia_output.h"
#include "okapia_theme.h"

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

// USB usage identifiers, which stop at okapia_input.cpp for everything except
// this: the zap is asked for by two keys that mean nothing to a menu, so there
// is no logical key to give them and inventing one would be worse.
static const unsigned char KEY_P = 0x13;
static const unsigned char KEY_R = 0x15;

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
        CLogger::Get ()->Write (FROM, LogNotice, "disk %s", Lines[i]);
    }

    // Which driver the Macintosh starts from. Written every time, including the
    // 0 that means "no preference": a card that once started from a disc would
    // otherwise go on asking for the CD driver after the disc was gone.
    const int nBootDriver = ChooserBootDriver (pChooser);
    PrefsReplaceInt32 ("bootdriver", nBootDriver);
    CLogger::Get ()->Write (FROM, LogNotice, "bootdriver %d", nBootDriver);

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
        CLogger::Get ()->Write (FROM, LogNotice, "cdrom %s", Lines[i]);
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
        CLogger::Get ()->Write (FROM, LogNotice, "engine %s", Lines[i]);
    }
    SavePrefs ();
}

// The parameter RAM, forgotten. main.cpp:106 rebuilds it from scratch as soon
// as the "NuMc" signature is missing, so removing the file *is* the zap — and
// it touches nothing of the user's data.
//
// f_unlink and not remove(): the newlib glue's remove() deletes the file and
// then leaves something behind that wedges the next fopen — the ROM never
// opened and the kernel stopped dead, with no log at all.
static void ForgetPram (void)
{
    const FRESULT nResult = f_unlink ("SD:/BasiliskII_XPRAM");
    CLogger::Get ()->Write (FROM, LogNotice, "Parameter RAM %s (%d)",
                            nResult == FR_OK ? "forgotten"
                                             : (nResult == FR_NO_FILE ? "was already absent"
                                                                      : "could not be removed"),
                            (int) nResult);
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
    pSettings->V.nFrameSkip = PrefsFindInt32 ("frameskip");
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

    // What this board can actually do. Circle builds USB audio for a Pi 4 and a
    // Pi 5 only (lib/sound/Makefile:37), so on a Pi 3 the entry is there and
    // grey — offering it and then falling silent would be the firmware lying.
    pSettings->nSoundAvailable = (1u << SoundOff) | (1u << SoundHDMI) | (1u << SoundJack);
#if RASPPI >= 4
    pSettings->nSoundAvailable |= 1u << SoundUSB;
#endif

    pSettings->Opened = pSettings->V;
}

static void SaveSettings (const TSettings *pSettings)
{
    PrefsReplaceInt32 ("ramsize", (int32) pSettings->V.nMemoryMB * 1024 * 1024);
    PrefsReplaceInt32 ("frameskip", pSettings->V.nFrameSkip);
    PrefsReplaceInt32 ("mousedpi", pSettings->V.nMouseDpi);

    PrefsReplaceString ("soundoutput", SOUND_NAMES[pSettings->V.nSound]);
    // Kept in step rather than replaced: upstream code and every prefs file
    // ever written for a desktop Basilisk II reads nosound, and a card carried
    // to one of those should still be silent when it was silent here.
    PrefsReplaceBool ("nosound", pSettings->V.nSound == SoundOff);

    PrefsReplaceString ("language", StringsCode ((TLanguage) pSettings->V.nLanguage));
    PrefsReplaceBool ("bootmenu", pSettings->V.bBootMenu);
    PrefsReplaceString ("extfs", pSettings->V.bShared
                                 ? (pSettings->V.SharedPath[0] != '\0'
                                    ? pSettings->V.SharedPath : SHARED_PATH)
                                 : "");
    PrefsReplaceString ("extfsname", pSettings->V.SharedName);
    SavePrefs ();

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Settings: %u MB, frameskip %d, sound %s, language %s, %s, %s",
                            pSettings->V.nMemoryMB, (int) pSettings->V.nFrameSkip,
                            SOUND_NAMES[pSettings->V.nSound],
                            StringsCode ((TLanguage) pSettings->V.nLanguage),
                            pSettings->V.bShared ? pSettings->V.SharedPath
                                                 : "no shared folder",
                            pSettings->V.bBootMenu ? "boot menu always"
                                                   : "boot menu on Option");
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
    InfoAdd (pInfo, Str (StrInfoVersion), __DATE__);
    InfoAdd (pInfo, Str (StrInfoMachine), CMachineInfo::Get ()->GetMachineName ());

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
        FILE *pFile = fopen (pROM, "rb");
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
        FATFS *pFS = 0;
        DWORD nFreeClusters = 0;
        if (f_getfree ("SD:", &nFreeClusters, &pFS) == FR_OK && pFS != 0)
        {
            const unsigned long nSector = 512;
            const unsigned long nTotalMB =
                (unsigned long) (pFS->n_fatent - 2) * pFS->csize * nSector / (1024 * 1024);
            const unsigned long nFreeMB =
                (unsigned long) nFreeClusters * pFS->csize * nSector / (1024 * 1024);
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
    InfoAdd (pInfo, Str (StrInfoCard), Line);

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
    InfoCredit (pInfo, Str (StrInfoAuthor), OKAPIA_AUTHOR);
    InfoCredit (pInfo, "Okapia",      "GPLv3+");
    InfoCredit (pInfo, "Basilisk II", "GPLv2+ · Christian Bauer et al.");
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
    void     (*Changed) (void);         // the selection moved; may be 0
    void     (*Relayout) (TSurface *);  // may be 0 when nothing can move
    // What Escape means on this page, or 0 when it means nothing. A dialogue
    // with two answers has a refusal and the key for it is Escape, exactly as
    // Return is the key for the assent: neither needs the keyboard to be
    // anywhere in particular, which is why an alert offers no navigation at
    // all.
    int      (*Cancel) (void);
};

static const int PageRelayout = -1;

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

    int nX = 0, nY = 0;
    bool bPointer = false;
    int nResult = 0;

    for (unsigned nTick = 0; nResult == 0; nTick++)
    {
        TEvent Event;
        bool bChanged = false;
        while (FwInputNext (&Event))
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
                    pDriver->Changed ();
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
                    if (nFocus >= 0 && nFocus < (int) nCount)
                    {
                        ScreenTouch (&Screen, nFocus);
                    }
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

        if (bChanged || (nTick % (500 / SLICE_MS) == 0 && ScreenBlinkCaret (&Screen))
            || bPointer)
        {
            FwInputPointer (&nX, &nY);
            ScreenPresent (pShadow, pOutput, &Screen, pDriver->Repaint, bPointer, nX, nY,
                           pTheme->nIconScale);
        }
        CTimer::Get ()->MsDelay (10);
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
static TInfo      s_Info;
static TConfirm   s_Confirm;
static TNewVolume s_NewVolume;

static int ChooserAdapter (int nIndex)
{
    // ChooserNothing is zero, which is what the driver reads as "stay here".
    return (int) ChooserOperate (&s_Chooser, nIndex, s_nCell);
}

static void ChooserChanged (void)
{
    ChooserSync (&s_Chooser);
}

static int SettingsAdapter (int nIndex)
{
    const TSettingsAction A = SettingsOperate (&s_Settings, nIndex);
    return A == SettingsRelayout ? PageRelayout : (int) A;
}

static void SettingsRelayoutPage (TSurface *pSurface)
{
    SettingsDraw (pSurface, &s_Settings);
}

static int InfoAdapter (int nIndex)
{
    return InfoIsBack (nIndex) ? 1 : 0;
}

// A page one turns to is a page one comes back from, and Escape is how: it
// costs nothing to offer and its absence is felt the first time somebody tries.
static int InfoCancelled (void)
{
    return 1;
}

static int SettingsCancelled (void)
{
    return (int) SettingsBack;
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
 *  succeeded, which is the one failure this project refuses to ship (AGENTS.md,
 *  §Data safety). A volume that grows on demand would turn a full card into a
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
    DWORD nFreeClusters = 0;
    FATFS *pFS = 0;
    if (f_getfree ("SD:", &nFreeClusters, &pFS) != FR_OK || pFS == 0)
    {
        CLogger::Get ()->Write (FROM, LogError, "Cannot read the card's free space");
        return 0;
    }
    // Clusters to megabytes, in that order: the multiplication overflows a
    // 32-bit count of bytes on any card worth using.
    const unsigned long nSectorsPerMB = (1024UL * 1024) / FF_MAX_SS;
    const unsigned long nSectors = (unsigned long) nFreeClusters * pFS->csize;
    return nSectorsPerMB == 0 ? 0 : nSectors / nSectorsPerMB;
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
        FILINFO Info;
        char Full[80];
        snprintf (Full, sizeof Full, "SD:%s", pOut);
        if (f_stat (Full, &Info) != FR_OK)
        {
            return true;
        }
    }
    return false;
}

// Create the file at its full size. f_lseek past the end of a file opened for
// writing is what allocates the clusters; f_expand would say so more plainly
// but FF_USE_EXPAND is 0 in circle-stdlib's ffconf.h, which lives in external/
// and is not ours to change.
static bool MakeImageFile (const char *pFullPath, unsigned long nMB)
{
    FIL File;
    if (f_open (&File, pFullPath, FA_CREATE_NEW | FA_WRITE) != FR_OK)
    {
        CLogger::Get ()->Write (FROM, LogError, "%s: cannot create", pFullPath);
        return false;
    }
    const FSIZE_t nBytes = (FSIZE_t) nMB * 1024 * 1024;
    const FRESULT nSeek = f_lseek (&File, nBytes);
    const bool bWhole = nSeek == FR_OK && f_tell (&File) == nBytes;
    f_close (&File);

    if (!bWhole)
    {
        // A file that could not be given its whole size is not a smaller
        // volume, it is a volume the Macintosh would run off the end of. It
        // goes, rather than being left for somebody to find later.
        CLogger::Get ()->Write (FROM, LogError,
                                "%s: only %lu MB could be allocated", pFullPath, nMB);
        f_unlink (pFullPath);
        return false;
    }
    return true;
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
        CLogger::Get ()->Write (FROM, LogError, "No free image name on the card");
        return;
    }
    char Full[80];
    snprintf (Full, sizeof Full, "SD:%s", Path);

    const unsigned long nMB = s_NewVolume.SizeMB[s_NewVolume.nPick];
    if (!MakeImageFile (Full, nMB))
    {
        return;
    }
    // Formatted through the path the emulator will use, not the FatFs one:
    // libhfs reaches the card through newlib, as it does everywhere else here.
    if (!HfsFormat (Path, s_NewVolume.Name))
    {
        // Nothing of the user's is in it — it was made a moment ago — and a
        // file that is not a volume would show up in the inventory as nothing
        // at all, which is worse than not showing up.
        f_unlink (Full);
        return;
    }
    CLogger::Get ()->Write (FROM, LogNotice, "%s: created, %lu MB, \"%s\"",
                            Path, nMB, s_NewVolume.Name);

    // The card changed, so what the chooser is showing no longer describes it.
    GatherVolumes (&s_Chooser);
}

/*
 *  Asking before doing
 */
static bool Ask (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme,
                 TConfirmLevel Level, TStringId Title, TStringId Body, TStringId Yes)
{
    s_Confirm.Level  = Level;
    s_Confirm.pTitle = Str (Title);
    s_Confirm.pBody  = Str (Body);
    s_Confirm.pYes   = Str (Yes);
    s_Confirm.pNo    = Str (StrCancel);
    ConfirmDraw (pShadow, &s_Confirm);

    static const TPageDriver Driver =
    {
        ConfirmRepaint, ConfirmWidgets, ConfirmAdapter, 0, 0, ConfirmCancelled
    };
    return RunPage (pOutput, pShadow, pTheme, &Driver) == (int) ConfirmYes;
}

/*
 *  The settings, and the information pane behind them
 */
// What Okapia found, on its own page. Reached from the main screen, which is
// where it is wanted: a machine that will not start is not a machine whose owner
// wants to go two clicks deep to find out why.
static void RunInfo (TSurface *pOutput, TSurface *pShadow, const TTheme *pTheme)
{
    GatherInfo (&s_Info, &s_Chooser, pOutput);
    InfoDraw (pShadow, &s_Info);

    static const TPageDriver Driver =
    {
        InfoRepaint, InfoWidgets, InfoAdapter, 0, 0, InfoCancelled
    };
    RunPage (pOutput, pShadow, pTheme, &Driver);
}

static TFirmwareResult RunSettings (TSurface *pOutput, TSurface *pShadow,
                                    const TTheme *pTheme)
{
    LoadSettings (&s_Settings);

    static const TPageDriver SettingsDriver =
    {
        SettingsRepaint, SettingsWidgets, SettingsAdapter, 0, SettingsRelayoutPage,
        SettingsCancelled
    };
    for (;;)
    {
        SettingsDraw (pShadow, &s_Settings);
        const int nAnswer = RunPage (pOutput, pShadow, pTheme, &SettingsDriver);

        if (nAnswer == (int) SettingsSave || nAnswer == (int) SettingsSaveRestart)
        {
            SaveSettings (&s_Settings);
            if (nAnswer == (int) SettingsSaveRestart)
            {
                return FirmwareReboot;
            }
        }
        return FirmwareBoot;            // Back, or saved and staying
    }
}

/*
 *  The firmware's screens, for as long as the user wants them
 */
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
        CLogger::Get ()->Write (FROM, LogError, "No room for a shadow surface");
        return FirmwareBoot;
    }

    static const TPageDriver ChooserDriver =
    {
        ChooserRepaint, ChooserWidgets, ChooserAdapter, ChooserChanged, 0
    };

    TFirmwareResult Result = FirmwareBoot;
    bool bDone = false;
    while (!bDone)
    {
        ChooserDraw (&Shadow, &s_Chooser);
        switch ((TChooserAction) RunPage (pOutput, &Shadow, pTheme, &ChooserDriver))
        {
        case ChooserStart:
            ApplyVolumes (&s_Chooser);
            bDone = true;
            break;

        case ChooserShutDown:
            // A note: the Macintosh has not started, so nothing is open and
            // nothing can be lost. A caution mark here would be crying wolf,
            // and the next one would be believed a little less.
            if (Ask (pOutput, &Shadow, pTheme, ConfirmNote, StrShutDownTitle,
                     StrShutDownBody, StrShutDown))
            {
                Result = FirmwareHalt;
                bDone  = true;
            }
            break;

        case ChooserForgetPram:
            // A caution: it changes something the Macintosh will not get back,
            // even though it touches none of the user's files.
            if (Ask (pOutput, &Shadow, pTheme, ConfirmCaution, StrForgetPramTitle,
                     StrForgetPramBody, StrForgetPram))
            {
                ForgetPram ();
            }
            break;

        case ChooserInformation:
            RunInfo (pOutput, &Shadow, pTheme);
            break;

        case ChooserNewVolume:
            RunNewVolume (pOutput, &Shadow, pTheme);
            break;

        case ChooserSettings:
            {
                const TFirmwareResult R = RunSettings (pOutput, &Shadow, pTheme);
                if (R != FirmwareBoot)
                {
                    Result = R;
                    bDone  = true;
                }
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
    // Claimed once for the life of the board and lent out, never taken and given
    // back: this runs again after every restart from Mac OS, and a mailbox
    // transaction repeated at each handover is one that has to succeed every
    // time. See okapia_output.h.
    CBcmFrameBuffer *pOutput = FwOutputClaim ();
    if (pOutput == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No frame buffer; going straight to the Mac");
        return FirmwareBoot;
    }

    const unsigned nWidth  = pOutput->GetWidth ();
    const unsigned nHeight = pOutput->GetHeight ();
    const unsigned nDepth  = pOutput->GetDepth ();

    if (nDepth != 32)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Expected 32 bpp, got %u; skipping", nDepth);
        return FirmwareBoot;
    }

    // The language before anything is drawn, since every label goes through it.
    // An unknown code answers the first language rather than failing: a card
    // written by a later version has to boot on an older firmware.
    StringsSetLanguage (StringsFromCode (PrefsFindString ("language")));

    TTheme Theme;
    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    ThemeMake (nScale16, &Theme);

    TSurface Surface;
    Surface.pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    Surface.nWidth  = nWidth;
    Surface.nHeight = nHeight;
    Surface.nPitch  = pOutput->GetPitch ();

    Theme.DrawDesktop (&Surface, &Theme);

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
    const bool bKeyboard = FwInputWatch ();
    FwInputBounds (nWidth, nHeight);

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
    CLogger::Get ()->Write (FROM, LogNotice, "Chooser: %u volume(s), startup %d",
                            s_Chooser.nCount, s_Chooser.nStartup);

    // Asked for once and for all: the window is skipped entirely, not shortened.
    // Two seconds of nothing before a menu one has asked to see every time is
    // two seconds of a machine looking broken — and on a board with no keyboard
    // attached, Option is not an answer at all.
    const bool bAlways = PrefsFindBool ("bootmenu");

    CLogger::Get ()->Write (FROM, LogNotice,
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
        while (FwInputNext (&Event))
        {
        }
        CTimer::Get ()->MsDelay (SLICE_MS);

        if (nStop == WINDOW_MS && (FwInputSeenModifiers () & ModOption))
        {
            const unsigned nEnd = nWaited + SLICE_MS + GRACE_MS;
            nStop = nEnd < WINDOW_MS ? nEnd : WINDOW_MS;
        }
    }

    const unsigned nSeen = FwInputSeenModifiers ();

    // Command-Option-P-R, the combination a Macintosh answered by forgetting its
    // parameter RAM. It is watched here rather than passed on, because a real
    // Macintosh does this in its ROM before the emulated ADB is alive at all:
    // letting it through would reach nobody.
    const bool bForgetPram = (nSeen & ModOption) && (nSeen & ModCommand)
                          && FwInputSeenKey (KEY_P) && FwInputSeenKey (KEY_R);

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
        // main.cpp:106 rebuilds the parameter RAM from scratch as soon as the
        // "NuMc" signature is missing, so removing the file *is* the zap — and
        // it touches nothing of the user's data.
        //
        // f_unlink and not remove(): the newlib glue's remove() deletes the file
        // and then leaves something behind that wedges the next fopen — the ROM
        // never opened and the kernel stopped dead after this point, with no log
        // at all. Going straight to the file system does the same job and the
        // boot carries on.
        const FRESULT nResult = f_unlink ("SD:/BasiliskII_XPRAM");
        CLogger::Get ()->Write (FROM, LogNotice, "Command-Option-P-R: parameter RAM %s (%d)",
                                nResult == FR_OK ? "forgotten"
                                                 : (nResult == FR_NO_FILE ? "was already absent"
                                                                          : "could not be removed"),
                                (int) nResult);
    }
    // Nothing to start from is not a state to hold a grey screen through and
    // then hand a question-mark floppy to: the firmware is the only thing that
    // can put it right, so it opens itself. This is the case a card written by
    // hand lands in, and the one where the machine looks broken and is not.
    else if (bWanted || s_Chooser.nStartup < 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                bAlways ? "Boot menu asked for: the chooser"
                                        : bOption ? "Option held: the chooser"
                                                  : "Nothing to start: the chooser");
        const TFirmwareResult Chosen = RunScreens (&Surface, &Theme);
        CLogger::Get ()->Write (FROM, LogNotice, "Display handed back");
        return Chosen;
    }
    else if (FwInputSeenAnything ())
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Input during the window, none of ours: modifiers %02X",
                                nSeen);
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Display handed back");
    return FirmwareBoot;
}
