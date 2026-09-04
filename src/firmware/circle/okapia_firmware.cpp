//
// okapia_firmware.cpp — the boot firmware, as the kernel sees it.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_firmware.h"

#include <circle/bcmframebuffer.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <wrap_fatfs.h>

// Basilisk II's headers, and sysdeps.h first as everywhere in that tree. The
// firmware reads exactly one preference and would rather not drag the emulator
// in for it, but declaring PrefsFindString by hand is how a signature drifts.
#include "sysdeps.h"
#include "prefs.h"

#include "hfs_volume_circle.h"

#include "okapia_chooser.h"
#include "okapia_gfx.h"
#include "okapia_screen.h"
#include "okapia_strings.h"
#include "okapia_input.h"
#include "okapia_output.h"
#include "okapia_theme.h"

#define FROM "firmware"

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
        THfsSystemVersion Version;
        if (Found[i].Blessed != 0 && HfsSystemVersion (Found[i].Path, &Version))
        {
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
        v->bMounted  = false;
        v->bReadOnly = false;
        pChooser->nCount++;
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
            pChooser->Volumes[i].bMounted  = true;
            pChooser->Volumes[i].bReadOnly = bReadOnly;
            if (pChooser->nStartup < 0 && pChooser->Volumes[i].bBootable)
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
 *  The chooser, for as long as the user wants it
 *
 *  Everything about painting a frame lives in ScreenPresent, so what is left
 *  here is the loop itself: take the events, hand them to the screen, and ask
 *  the chooser what a control meant.
 */
static TFirmwareResult RunChooser (TSurface *pOutput, const TTheme *pTheme)
{
    TChooser Model;
    GatherVolumes (&Model);
    CLogger::Get ()->Write (FROM, LogNotice, "Chooser: %u volume(s), startup %d",
                            Model.nCount, Model.nStartup);

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

    ChooserDraw (&Shadow, &Model);
    TWidget *pWidgets = 0;
    const unsigned nCount = ChooserWidgets (&pWidgets);

    TScreen Screen;
    ScreenInit (&Screen, pTheme, pWidgets, nCount);
    Screen.Background = ColorWhite;
    Screen.Bounds     = Rect (0, 0, pOutput->nWidth, pOutput->nHeight);
    ChooserRepaint (&Shadow);
    GfxBlit (pOutput, &Shadow, Screen.Bounds);
    TRect Ignored;
    ScreenPaintDirty (&Shadow, &Screen, &Ignored);

    int nX = 0, nY = 0;
    bool bPointer = false;
    TFirmwareResult Result = FirmwareBoot;
    bool bDone = false;

    for (unsigned nTick = 0; !bDone; nTick++)
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
                // Moving in the list changes what the controls underneath are
                // true of, so they follow it.
                bChanged = true;
                ChooserSync (&Model);
            }
            else if (Reply.Result == ScreenActivated)
            {
                bChanged = true;
                switch (ChooserOperate (&Model, Reply.nIndex))
                {
                case ChooserStart:
                    ApplyVolumes (&Model);
                    bDone = true;
                    break;

                case ChooserShutDown:
                    Result = FirmwareHalt;
                    bDone = true;
                    break;

                case ChooserForgetPram:
                    ForgetPram ();
                    break;

                case ChooserSettings:
                    CLogger::Get ()->Write (FROM, LogNotice, "Settings: phase 16h");
                    break;

                default:
                    break;
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
            ScreenPresent (&Shadow, pOutput, &Screen, ChooserRepaint, bPointer, nX, nY,
                           pTheme->nIconScale);
        }
        CTimer::Get ()->MsDelay (10);
    }

    delete[] Shadow.pPixels;
    return Result;
}

TFirmwareResult FirmwareRun (void)
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

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Output %ux%u, theme scale %u/16, language %s, %s, holding %u ms",
                            nWidth, nHeight, nScale16,
                            StringsCode (StringsLanguage ()),
                            bKeyboard ? "keyboard attached" : "no keyboard", WINDOW_MS);

    for (unsigned nWaited = 0; nWaited < WINDOW_MS; nWaited += SLICE_MS)
    {
        // Drained rather than read, so that a window nobody touches cannot end
        // with a full queue: this one only wants the latch, but the queue is
        // the same one the screens will read from.
        TEvent Event;
        while (FwInputNext (&Event))
        {
        }
        CTimer::Get ()->MsDelay (SLICE_MS);
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
    else if (bOption)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Option held: the chooser");
        const TFirmwareResult Chosen = RunChooser (&Surface, &Theme);
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
