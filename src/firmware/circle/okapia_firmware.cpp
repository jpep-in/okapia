//
// okapia_firmware.cpp — the boot firmware, as the kernel sees it.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_firmware.h"

#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/types.h>

#include <wrap_fatfs.h>

#include "okapia_gfx.h"
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

/*
 *  What the window watched happen
 *
 *  Circle hands over the whole keyboard state rather than events, so the reply
 *  is a set of things seen at any moment, not a sequence. That suits the
 *  question being asked: not "what did the user type" but "was Option held".
 */
// Everything is latched over the whole window rather than read at an instant.
// A report arrives on every change, release included, so the four parts of
// Command-Option-P-R are rarely all present in one of them — the first attempt
// asked for that coincidence and missed the combination it was watching for.
// Latching also matches what a Macintosh did, which sampled the keyboard's
// state through the window rather than demanding a single perfect moment.
static volatile unsigned      s_nSeen;          // TSeen bits, latched
static volatile bool          s_bAnyKey;        // anything at all, for the log
// What actually arrived, for the log alone. A window that saw nothing it wanted
// should be able to say what it did see; guessing at that from the outside is
// how one ends up blaming the keyboard for a wrong constant.
static volatile unsigned char s_nModsSeen;
static volatile unsigned char s_nFirstKey;

// HID usage IDs of the keys the window cares about.
static const unsigned char KEY_P = 0x13;
static const unsigned char KEY_R = 0x15;

enum TSeen
{
    SeenP     = 1u << 0,
    SeenR     = 1u << 1,
    SeenAlt   = 1u << 2,
    SeenGui   = 1u << 3,
    SeenOther = 1u << 4                 // Control or Shift: not ours
};

// Left and right alike: a keyboard's two Option keys mean the same thing, and a
// user holding the right one is not asking for something else.
static const unsigned char MOD_ALT = 0x04 | 0x40;
static const unsigned char MOD_GUI = 0x08 | 0x80;       // Command
static const unsigned char MOD_CTRL = 0x01 | 0x10;
static const unsigned char MOD_SHIFT = 0x02 | 0x20;

static void FirmwareKeyHandler (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    unsigned nSeen = s_nSeen;

    if (ucModifiers & MOD_ALT)                  nSeen |= SeenAlt;
    if (ucModifiers & MOD_GUI)                  nSeen |= SeenGui;
    if (ucModifiers & (MOD_CTRL | MOD_SHIFT))   nSeen |= SeenOther;

    for (unsigned i = 0; i < 6; i++)
    {
        if (RawKeys[i] == 0)
        {
            continue;
        }
        s_bAnyKey = true;
        if (RawKeys[i] == KEY_P) nSeen |= SeenP;
        if (RawKeys[i] == KEY_R) nSeen |= SeenR;
    }
    if (ucModifiers != 0)
    {
        s_bAnyKey = true;
    }
    s_nModsSeen = (unsigned char) (s_nModsSeen | ucModifiers);
    if (s_nFirstKey == 0 && RawKeys[0] != 0)
    {
        s_nFirstKey = RawKeys[0];
    }
    s_nSeen = nSeen;
}

TFirmwareResult FirmwareRun (void)
{
    // 0, 0 asks the firmware for the display's own size; what it grants is
    // reported rather than assumed, because on a Pi 5 the request is ignored
    // outright and under QEMU the answer is a device property.
    CBcmFrameBuffer *pOutput = new CBcmFrameBuffer (0, 0, 32);
    if (pOutput == 0 || !pOutput->Initialize ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No frame buffer; going straight to the Mac");
        delete pOutput;
        return FirmwareBoot;
    }

    const unsigned nWidth  = pOutput->GetWidth ();
    const unsigned nHeight = pOutput->GetHeight ();
    const unsigned nDepth  = pOutput->GetDepth ();

    if (nDepth != 32)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Expected 32 bpp, got %u; skipping", nDepth);
        delete pOutput;
        return FirmwareBoot;
    }

    TTheme Theme;
    const unsigned nScale16 = ThemeScaleFor (nWidth, nHeight);
    ThemeMake (nScale16, &Theme);

    TSurface Surface;
    Surface.pPixels = (unsigned char *) (uintptr) pOutput->GetBuffer ();
    Surface.nWidth  = nWidth;
    Surface.nHeight = nHeight;
    Surface.nPitch  = pOutput->GetPitch ();

    Theme.DrawDesktop (&Surface, &Theme);

    // The keyboard, taken for the length of the window and *not* given back.
    // Circle keeps one raw handler and InputInit() puts the Macintosh's in its
    // place when StartMacintosh() runs, which is the whole handover.
    //
    // Registering 0 to detach looks tidier and wedges the boot: it does not
    // detach anything, it puts the device back into cooked mode
    // (usbkeyboard.cpp:200), and the reports then arriving go to
    // CKeyboardBehaviour instead. Measured — with that call the kernel stopped
    // dead after this function returned, with no further log at all, and
    // removing it was enough. Leaving our handler in place until InputInit()
    // replaces it costs nothing: it only ever touches its own statics.
    CUSBKeyboardDevice *pKeyboard = (CUSBKeyboardDevice *)
        CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
    if (pKeyboard != 0)
    {
        s_nSeen     = 0;
        s_bAnyKey   = false;
        s_nModsSeen = 0;
        s_nFirstKey = 0;
        pKeyboard->RegisterKeyStatusHandlerRaw (FirmwareKeyHandler);
    }

    CLogger::Get ()->Write (FROM, LogNotice, "Output %ux%u, theme scale %u/16, %s, holding %u ms",
                            nWidth, nHeight, nScale16,
                            pKeyboard != 0 ? "keyboard attached" : "no keyboard", WINDOW_MS);

    for (unsigned nWaited = 0; nWaited < WINDOW_MS; nWaited += SLICE_MS)
    {
        CTimer::Get ()->MsDelay (SLICE_MS);
    }

    const unsigned nSeen = s_nSeen;

    // Command-Option-P-R, the combination a Macintosh answered by forgetting its
    // parameter RAM. It is watched here rather than passed on, because a real
    // Macintosh does this in its ROM before the emulated ADB is alive at all:
    // letting it through would reach nobody.
    const bool bForgetPram = (nSeen & SeenAlt) && (nSeen & SeenGui)
                          && (nSeen & SeenP)   && (nSeen & SeenR);

    // Option on its own opens the chooser. Every other combination including it
    // belongs to the Macintosh, so anything else held alongside disqualifies it.
    const bool bOption = !bForgetPram
                      && (nSeen & SeenAlt)
                      && !(nSeen & (SeenGui | SeenOther));

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
        CLogger::Get ()->Write (FROM, LogNotice, "Option held: the chooser would open here");
    }
    else if (s_bAnyKey)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Keys during the window, none of ours: modifiers %02X, first key %02X",
                                s_nModsSeen, s_nFirstKey);
    }

    // Handed back before the emulator claims its own. Measured under QEMU: a
    // frame buffer can be claimed, released and claimed again, same address and
    // same size, and the second one is live. Worth re-checking on hardware —
    // the mailbox is the firmware's, not ours.
    delete pOutput;

    CLogger::Get ()->Write (FROM, LogNotice, "Display handed back");
    return FirmwareBoot;
}
