/*
 * input_circle.cpp — USB keyboard and mouse, presented to the Mac as ADB.
 *
 * Circle reports raw HID; the Mac wants Apple Desktop Bus key codes, which bear
 * no relation to HID numbering. The table below maps the keys a Mac keyboard
 * actually has, and unmapped keys are dropped rather than sent as something
 * else — a wrong key code is worse than a missing one.
 *
 * Events are pushed into adb.cpp and flagged with INTFLAG_ADB; the core then
 * calls ADBInterrupt() from its interrupt path (emul_op.cpp:512).
 *
 * They are not pushed from here, though. Circle's USB handlers run on core 0
 * and the emulator runs on another, while adb.cpp's key ring — key_buffer[],
 * key_write_ptr, the key matrix — carries no lock at all: upstream gets away
 * with it because x86 publishes stores in order, and AArch64 does not. So the
 * handlers below only record, and InputDrain() hands the events to adb.cpp from
 * the emulation thread, where nothing else is looking. It is the shape
 * infinite-mac uses for the same reason (JS/input_js.cpp:15).
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "okapia_circle.h"
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
#include <circle/devicenameservice.h>

#include "okapia_input.h"
#include <string.h>

#include "adb.h"
#include "cpu_emulation.h"
#include "prefs.h"
#include "main.h"

#define FROM "okapia-input"

static CUSBKeyboardDevice *s_pKeyboard;
static CMouseDevice       *s_pMouse;
static unsigned char       s_LastKeys[6];
static unsigned char       s_LastModifiers;
static unsigned            s_LastMouseButtons;

// How often the pointer is sampled, and how often it reaches the Macintosh.
// The second number is the one that matters and the one that used to lie: the
// drain is armed by an opcode count, so its rate follows the engine's speed
// rather than the clock. cpu_ticks_circle.cpp now holds it to a rate; this is
// what says whether it does.
static volatile unsigned s_nReports;
static unsigned s_nDrains, s_nMotionDrains;

/*
 *  The queue between the two cores
 *
 *  One producer (Circle's USB handlers, core 0) and one consumer (InputDrain(),
 *  the emulation core). Keys and buttons are discrete and must neither be lost
 *  nor reordered, so they go through a ring; motion is continuous and is
 *  accumulated instead, which bounds the queue and keeps the pointer smooth
 *  when a drain is late.
 *
 *  The barriers are the whole point of the exercise. The producer publishes the
 *  slot contents *before* the write index with a release store; the consumer
 *  reads the index with an acquire load. Without that pair a report can be
 *  announced before it is written, and the Mac types a key that was never
 *  pressed. Nothing here allocates, and nothing here blocks.
 */

enum TInputKind
{
    InputKeyDown, InputKeyUp, InputButtonDown, InputButtonUp
};

struct TInputEvent
{
    unsigned char nKind;
    unsigned char nCode;
};

// A power of two, so the compiler turns the wrap into a mask. A key costs two
// events and nobody types faster than ten a second, which makes 64 about three
// seconds of headroom against a drain that comes round every four milliseconds.
static const unsigned INPUT_QUEUE_SIZE = 64;

static TInputEvent s_Queue[INPUT_QUEUE_SIZE];
static volatile unsigned s_nQueueWrite;     // producer only
static volatile unsigned s_nQueueRead;      // consumer only
static volatile unsigned s_nQueueDropped;

static void QueuePost (TInputKind Kind, unsigned char nCode)
{
    unsigned nWrite = s_nQueueWrite;
    unsigned nNext  = (nWrite + 1) % INPUT_QUEUE_SIZE;

    if (nNext == __atomic_load_n (&s_nQueueRead, __ATOMIC_ACQUIRE))
    {
        // Full. Dropping a key-up would leave a key held down for ever, so this
        // is counted and reported rather than passed over: if it ever fires,
        // the drain is not running and that is the fault to find.
        s_nQueueDropped++;
        return;
    }

    s_Queue[nWrite].nKind = (unsigned char) Kind;
    s_Queue[nWrite].nCode = nCode;
    __atomic_store_n (&s_nQueueWrite, nNext, __ATOMIC_RELEASE);
}

#ifdef SHEEPSHAVER
// The pointer's position, which this engine reports absolutely: only the latest
// one matters, so it is a slot and not a queue.
static volatile int s_nPostedX, s_nPostedY;
static volatile unsigned s_bPostedMove;
#else
// Relative deltas, which have to be summed and not overwritten.
static volatile int s_nPendingDX, s_nPendingDY;
#endif

/*
 *  USB HID usage -> Mac ADB key code.
 *
 *  Not written by hand. SDL2 scancodes *are* USB HID usage IDs — SDL defines
 *  its scancode set from the HID Keyboard/Keypad page — so the SDL2 section of
 *  Basilisk's own keycodes file is already the table a USB host needs, and
 *  scripts/gen-keycodes.py turns it into the array below at build time.
 *
 *  Deriving it is not pedantry. The hand-written version this replaces had the
 *  four arrows wrong, because ADBKeyDown() wants *raw ADB* codes and the
 *  familiar numbers are the Mac virtual key codes — which agree for letters and
 *  digits and differ for the arrows. Up went out as 0x7E, landed beside the
 *  Power key, and opened the shutdown dialog on every press. The generated
 *  table also brings the numeric keypad, which the hand-written one omitted
 *  entirely.
 *
 *  A card may override it: see KeycodesLoad().
 */
static unsigned char s_HIDToMac[256] =
#include "keycodes_default.h"
;

/*
 *  Replace the table from a keycodes file on the card.
 *
 *  Same format as Basilisk's: a driver name on its own line, then "scancode
 *  keycode" pairs in decimal, # or ; for comments. The section is the one
 *  headed "sdl cocoa" — Basilisk's name for the SDL2 table — or "okapia" for a
 *  file written for this machine. Anything else in the file is skipped, so an
 *  unmodified BasiliskII.keycodes works as it stands.
 *
 *  Partial overrides are allowed and useful: a file with two lines fixes two
 *  keys and leaves the rest alone.
 */

bool KeycodesLoad (const char *pPath)
{
    FILE *pFile = fopen (pPath, "r");
    if (pFile == 0)
    {
        return false;
    }

    char Line[128];
    bool bInSection = false;
    bool bHeaderRun = false;        // reading a run of driver names, not pairs
    unsigned nApplied = 0;

    while (fgets (Line, sizeof Line, pFile) != 0)
    {
        char *p = strpbrk (Line, "#;\r\n");
        if (p != 0)
        {
            *p = '\0';
        }
        while (*Line != '\0' && isspace ((unsigned char) Line[strlen (Line) - 1]))
        {
            Line[strlen (Line) - 1] = '\0';
        }
        char *pStart = Line;
        while (isspace ((unsigned char) *pStart))
        {
            pStart++;
        }
        if (*pStart == '\0')
        {
            continue;
        }

        if (!isdigit ((unsigned char) *pStart))
        {
            // A driver name. Several may stack in front of one table, so a run
            // of them selects the section together.
            if (!bHeaderRun)
            {
                bInSection = false;
                bHeaderRun = true;
            }
            if (strcmp (pStart, "sdl cocoa") == 0 || strcmp (pStart, "okapia") == 0)
            {
                bInSection = true;
            }
            continue;
        }

        bHeaderRun = false;
        if (!bInSection)
        {
            continue;
        }

        int nScan = -1, nMac = -1;
        if (sscanf (pStart, "%d %d", &nScan, &nMac) != 2)
        {
            continue;
        }
        if (nScan < 0 || nScan > 255 || nMac < 0 || nMac > 255)
        {
            continue;
        }
        s_HIDToMac[nScan] = (unsigned char) nMac;
        nApplied++;
    }
    fclose (pFile);

    if (nApplied == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "%s: no \"sdl cocoa\" or \"okapia\" section, keyboard unchanged",
                                pPath);
        return false;
    }

    CLogger::Get ()->Write (FROM, LogNotice, "%s: %u key mappings applied", pPath, nApplied);
    return true;
}

static void PostKey (unsigned char nHID, bool bDown)
{
    unsigned char nMac = s_HIDToMac[nHID];
    if (nMac == 0xFF)
    {
        return;                 // no Mac equivalent: drop it
    }

    QueuePost (bDown ? InputKeyDown : InputKeyUp, nMac);
}

/*
 *  Circle hands us the full keyboard state, not events, so changes have to be
 *  derived by comparing with the previous report.
 */
static void KeyStatusHandler (unsigned char ucModifiers, const unsigned char RawKeys[6])
{
    // Modifiers, bit by bit: LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI
    static const unsigned char ModToMac[8] =
    {
        0x36,   // left control -> Control
        0x38,   // left shift
        0x3A,   // left option
        0x37,   // left GUI -> Command
        0x36, 0x38, 0x3A, 0x37
    };
    for (unsigned i = 0; i < 8; i++)
    {
        unsigned char nBit = 1 << i;
        bool bWas = (s_LastModifiers & nBit) != 0;
        bool bIs  = (ucModifiers & nBit) != 0;
        if (bWas != bIs)
        {
            QueuePost (bIs ? InputKeyDown : InputKeyUp, ModToMac[i]);
        }
    }
    s_LastModifiers = ucModifiers;

    // Released: in the old report, absent from the new one.
    for (unsigned i = 0; i < 6; i++)
    {
        unsigned char k = s_LastKeys[i];
        if (k == 0) continue;
        bool bStillDown = false;
        for (unsigned j = 0; j < 6; j++)
        {
            if (RawKeys[j] == k) { bStillDown = true; break; }
        }
        if (!bStillDown) PostKey (k, false);
    }

    // Pressed: in the new report, absent from the old one.
    for (unsigned i = 0; i < 6; i++)
    {
        unsigned char k = RawKeys[i];
        if (k == 0) continue;
        bool bWasDown = false;
        for (unsigned j = 0; j < 6; j++)
        {
            if (s_LastKeys[j] == k) { bWasDown = true; break; }
        }
        if (!bWasDown) PostKey (k, true);
    }

    memcpy (s_LastKeys, RawKeys, sizeof s_LastKeys);
}

#ifdef SHEEPSHAVER
// sheepshaver/video_circle.cpp: how far the pointer may go; it moves with the
// mode, so the file that owns the mode owns the answer.
extern void VideoMacScreenSize (unsigned *pWidth, unsigned *pHeight);

// Where the Macintosh's pointer is, because on this engine we have to tell it.
static int s_nMouseX, s_nMouseY;

/*
 *  The Macintosh's own acceleration, applied here because it cannot apply it
 *
 *  This engine feeds the Mac a position through CursorDeviceDispatch's MoveTo
 *  (adb.cpp:405), and MoveTo sets the cursor where it is told. Only Move --
 *  selector 0 -- runs deltas through the acceleration tables (CrsrDev.a:137),
 *  so the Mouse control panel has nothing to act on and its slider is inert.
 *  Calling selector 0 instead would mean executing 68k on every report; this
 *  does the same arithmetic on our side of the fence, once, with no guest code.
 *
 *  The curve is not invented. It is the ROM's own 'accl' resource for a mouse
 *  (MiscROMRsrcs.r, the table marked "New, better-feeling"), in Fixed 16.16,
 *  and the units are settled by CrsrDev.a:1257 -- the numbers are inches per
 *  second, scaled into device counts by dpi/frameRate and into screen pixels by
 *  72/frameRate. Three properties none of a hand-made curve would have had:
 *  the gain is *below* one at very low speed, so the Mac slows the pointer to
 *  let you aim; the gain peaks in the middle, near eight, rather than at the
 *  top; and the output saturates at 150 in/s, which is a speed ceiling and not
 *  a gain ceiling.
 *
 *  The control panel picks a Fixed between 0 and 1 and the ROM interpolates
 *  between the identity table and this one (CrsrDev.a:1102). That is why the
 *  slider's leftmost position is a tablet: at zero the table *is* the identity.
 *  SPVolCtl's three bits are that setting, measured on System 7.1 and on Mac OS
 *  8.6 alike, 0 to 6 in both.
 */
struct TAccelPoint { int nIn, nOut; };          // inches/second, Fixed 16.16

static const TAccelPoint ACCEL_CURVE[] =
{
    { 0x0000713B, 0x00006000 },     //  0.44 ->   0.38, a gain below one
    { 0x00044EC5, 0x00108000 },     //  4.31 ->  16.50
    { 0x000C0000, 0x005F0000 },     // 12.00 ->  95.00, the peak, about 8x
    { 0x0016EC4F, 0x008B0000 },     // 22.93 -> 139.00
    { 0x001D3B14, 0x00948000 },     // 29.23 -> 148.50
    { 0x00227627, 0x00960000 },     // 34.46 -> 150.00, saturated from here
    { 0x00280000, 0x00960000 },     // 40.00 -> 150.00
};
static const unsigned ACCEL_POINTS = sizeof ACCEL_CURVE / sizeof ACCEL_CURVE[0];

// The Macintosh screen's own resolution, which the ROM states as a constant
// (CrsrDev.a:1764) rather than measuring.
static const int SCREEN_DPI = 72;

// What the Mac assumes a mouse reports (CrsrDev.a:2045). Yours is nothing like
// it, which is the whole reason the "mousedpi" preference exists.
static const int PERIOD_MOUSE_DPI = 200;

static int s_nMouseDpi = 1000;

// The pointer's position keeps its fraction, so a slow hand is never rounded
// away to nothing, and the clock the speed is measured against.
static int      s_nCarryX, s_nCarryY;       // 1/65536 of a pixel
static unsigned s_nLastReportAt;

// The control panel's setting, 0 to 6, read from the guest by the drain.
static volatile unsigned s_nTracking = 3;


// Fixed 16.16 throughout, as the ROM does it: this runs in a USB interrupt on
// core 0, where floating point has no business being.
static int CurveLookup (int nSpeed)
{
    if (nSpeed <= 0)
    {
        return 0;
    }
    if (nSpeed <= ACCEL_CURVE[0].nIn)
    {
        // Below the first point the ROM's table starts at a gain under one, and
        // straight-lining to the origin keeps that: it is what makes a slow
        // hand able to aim.
        return (int) ((int64) nSpeed * ACCEL_CURVE[0].nOut / ACCEL_CURVE[0].nIn);
    }
    for (unsigned i = 1; i < ACCEL_POINTS; i++)
    {
        if (nSpeed <= ACCEL_CURVE[i].nIn)
        {
            const int x0 = ACCEL_CURVE[i - 1].nIn, y0 = ACCEL_CURVE[i - 1].nOut;
            const int x1 = ACCEL_CURVE[i].nIn,     y1 = ACCEL_CURVE[i].nOut;
            return y0 + (int) ((int64) (nSpeed - x0) * (y1 - y0) / (x1 - x0));
        }
    }
    return ACCEL_CURVE[ACCEL_POINTS - 1].nOut;      // the ceiling
}

/*
 *  One axis, one report
 *
 *  The ROM interpolates between the identity table and the curve by a Fixed
 *  between 0 and 1 (CrsrDev.a:1102); the slider's seven positions are that
 *  number, so 0 is the tablet and gives movement back untouched.
 */
static int Advance (int nDelta, int *pCarry, unsigned nElapsedUsec, int nTracking)
{
    if (nDelta == 0)
    {
        return 0;
    }
    const int nSign = nDelta < 0 ? -1 : 1;
    const int nCount = nDelta * nSign;

    // counts -> inches/second, Fixed 16.16.
    const int nSpeed = (int) (((int64) nCount << 16) * 1000000
                              / ((int64) s_nMouseDpi * nElapsedUsec));

    int nOut = CurveLookup (nSpeed);

    // Blend with the identity by the slider, which is what the ROM does rather
    // than scaling the result: at 0 the pointer follows the hand exactly.
    if (nTracking < 6)
    {
        nOut = (int) (((int64) nOut * nTracking
                       + (int64) nSpeed * (6 - nTracking)) / 6);
    }

    // inches/second -> pixels, and keep the fraction for the next report.
    *pCarry += nSign * (int) (((int64) nOut * SCREEN_DPI * nElapsedUsec) / 1000000);
    const int nWhole = *pCarry >> 16;
    *pCarry -= nWhole << 16;
    return nWhole;
}
#endif

static void MouseStatusHandler (unsigned nButtons, int nDeltaX,
                                int nDeltaY, int nWheelMove)
{
    (void) nWheelMove;

    if (nDeltaX != 0 || nDeltaY != 0)
    {
        s_nReports++;
#ifdef SHEEPSHAVER
        /*
         *  Counts to inches per second, through the curve, back to pixels.
         *
         *  Speed needs a clock, not a frame: the ROM divides by its frameRate
         *  of 67 because that is when it recomputes, and we are handed reports
         *  whenever the mouse has something to say. Measuring the interval is
         *  both simpler and more honest than assuming one.
         */
        const unsigned nNow = CTimer::GetClockTicks ();
        unsigned nElapsed = nNow - s_nLastReportAt;
        s_nLastReportAt = nNow;
        if (nElapsed == 0 || nElapsed > 200000)
        {
            nElapsed = 10000;   // first report, or a hand that stopped: assume 10 ms
        }

        const int nTracking = (int) __atomic_load_n (&s_nTracking, __ATOMIC_RELAXED);
        s_nMouseX += Advance (nDeltaX, &s_nCarryX, nElapsed, nTracking);
        s_nMouseY += Advance (nDeltaY, &s_nCarryY, nElapsed, nTracking);

        unsigned nWidth = 640, nHeight = 480;
        VideoMacScreenSize (&nWidth, &nHeight);
        if (s_nMouseX < 0) s_nMouseX = 0;
        if (s_nMouseY < 0) s_nMouseY = 0;
        if (s_nMouseX > (int) nWidth  - 1) s_nMouseX = (int) nWidth  - 1;
        if (s_nMouseY > (int) nHeight - 1) s_nMouseY = (int) nHeight - 1;
        s_nPostedX = s_nMouseX;
        s_nPostedY = s_nMouseY;
        __atomic_store_n (&s_bPostedMove, 1, __ATOMIC_RELEASE);
#else
        // Summed, not stored: every delta has to reach the Mac or the pointer
        // ends up somewhere the hand did not put it. They go on to adb.cpp's
        // relative branch, which packs them into an ADB report and runs the
        // Macintosh's own mouse driver — so the acceleration, and the Mouse
        // control panel that sets it, are the Macintosh's own and not ours.
        __atomic_fetch_add (&s_nPendingDX, nDeltaX, __ATOMIC_RELAXED);
        __atomic_fetch_add (&s_nPendingDY, nDeltaY, __ATOMIC_RELAXED);
#endif
    }

    static const unsigned ButtonMasks[] =
    {
        MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE
    };
    unsigned nChanged = nButtons ^ s_LastMouseButtons;
    for (unsigned i = 0; i < sizeof ButtonMasks / sizeof ButtonMasks[0]; i++)
    {
        if (nChanged & ButtonMasks[i])
        {
            QueuePost ((nButtons & ButtonMasks[i]) ? InputButtonDown
                                                   : InputButtonUp,
                       (unsigned char) i);
        }
    }
    s_LastMouseButtons = nButtons;
}

/*
 *  The drain, and the only place that touches adb.cpp
 *
 *  Called from the emulation thread — cpu_do_check_ticks() on the 68k engine,
 *  the periodic seam in the PowerPC interpreter on the other — so roughly every
 *  4 ms, which is a quarter of a Mac tick and therefore free as far as the
 *  guest can tell. Everything it calls mutates state the emulator reads on this
 *  same core, which is the point.
 */

static void InputReport (void)
{
    // The drain now comes round a thousand times a second, so the clock is not
    // read on every visit for a report that speaks every five: a counter
    // decides when it is worth asking.
    static unsigned s_nUntilLook;
    if (s_nUntilLook-- != 0)
    {
        return;
    }
    s_nUntilLook = 256;

    static unsigned s_nLast;
    const unsigned nNow = CTimer::Get ()->GetTicks () / HZ;
    if (nNow == s_nLast || (nNow % 5) != 0)
    {
        return;
    }
    s_nLast = nNow;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "mouse: %u USB reports, %u drains (%u carried the "
                            "pointer)",
                            s_nReports, s_nDrains, s_nMotionDrains);
    s_nReports = 0;
    s_nDrains = s_nMotionDrains = 0;
}

/*
 *  Where the Macintosh keeps the pointer's speed, and how we know
 *
 *  SPVolCtl (0x208), bits 5:3 — the field the PRAM template calls "mouse
 *  tracking" (SysUtil.a:1100). Not deduced: measured, by sweeping the Mouse
 *  control panel in both directions and watching which of the ROM's plausible
 *  homes moved. CrsrThresh (0x8EC) never left its startup 6 and parameter RAM
 *  never moved at all, on System 7.1 and on Mac OS 8.6 alike, while SPVolCtl
 *  followed every position: seven of them, the tablet at 0 and "Fast" at 6.
 *
 *  Read here because this runs on the emulation core; the USB handler that
 *  needs it must not reach into guest memory itself. Costly enough to be worth
 *  a counter and no more: somebody changing a control panel is not in a hurry.
 */
static void ReadMouseSettings (void)
{
    static unsigned s_nUntil;
    if (s_nUntil-- != 0)
    {
        return;
    }
    s_nUntil = 512;

    const unsigned nTracking = (ReadMacInt8 (0x208) >> 3) & 7;
    const unsigned nScaling  = (ReadMacInt8 (0x20B) & 0x40) != 0 ? 1u : 0u;
    const unsigned nThresh   = ReadMacInt16 (0x8EC);
#ifdef SHEEPSHAVER
    // Published to the USB handler, which must not read guest memory itself.
    __atomic_store_n (&s_nTracking, nScaling != 0 ? nTracking : 0u,
                      __ATOMIC_RELAXED);
#endif

    static unsigned s_nSaidT = 99, s_nSaidS = 99, s_nSaidC = 0xFFFF;
    if (nTracking != s_nSaidT || nScaling != s_nSaidS || nThresh != s_nSaidC)
    {
        s_nSaidT = nTracking;
        s_nSaidS = nScaling;
        s_nSaidC = nThresh;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Mouse control panel: tracking %u/6, scaling %u, "
                                "CrsrThresh %u",
                                nTracking, nScaling, nThresh);
    }
}

void InputDrain (void)
{
    s_nDrains++;
    ReadMouseSettings ();

    // Every one of these six calls raises INTFLAG_ADB and triggers the
    // interrupt itself (adb.cpp:248, :264, :312), so nothing here does.
    unsigned nWrite = __atomic_load_n (&s_nQueueWrite, __ATOMIC_ACQUIRE);
    while (s_nQueueRead != nWrite)
    {
        const TInputEvent Event = s_Queue[s_nQueueRead];

        switch (Event.nKind)
        {
        case InputKeyDown:      ADBKeyDown (Event.nCode);   break;
        case InputKeyUp:        ADBKeyUp (Event.nCode);     break;
        case InputButtonDown:   ADBMouseDown (Event.nCode); break;
        case InputButtonUp:     ADBMouseUp (Event.nCode);   break;
        }

        __atomic_store_n (&s_nQueueRead, (s_nQueueRead + 1) % INPUT_QUEUE_SIZE,
                          __ATOMIC_RELEASE);
    }

#ifdef SHEEPSHAVER
    if (__atomic_exchange_n (&s_bPostedMove, 0, __ATOMIC_ACQUIRE))
    {
        ADBMouseMoved (s_nPostedX, s_nPostedY);
        s_nMotionDrains++;
    }
#else
    int nDX = __atomic_exchange_n (&s_nPendingDX, 0, __ATOMIC_RELAXED);
    int nDY = __atomic_exchange_n (&s_nPendingDY, 0, __ATOMIC_RELAXED);
    if (nDX != 0 || nDY != 0)
    {
        ADBMouseMoved (nDX, nDY);
        s_nMotionDrains++;
    }
#endif

    if (s_nQueueDropped != 0)
    {
        static unsigned s_nReported;
        unsigned nNow = s_nQueueDropped;
        if (nNow != s_nReported)
        {
            s_nReported = nNow;
            CLogger::Get ()->Write (FROM, LogWarning,
                                    "%u input event(s) dropped: the drain is late",
                                    nNow);
        }
    }

    InputReport ();
}

/*
 *  Attach whatever is plugged in. Called repeatedly: USB devices may appear
 *  after boot, and Circle only reports them once enumerated.
 */

void InputAttachDevices (void)
{
    if (s_pKeyboard == 0)
    {
        s_pKeyboard = (CUSBKeyboardDevice *)
            CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
        if (s_pKeyboard != 0)
        {
            s_pKeyboard->RegisterKeyStatusHandlerRaw (KeyStatusHandler);
            CLogger::Get ()->Write (FROM, LogNotice, "Keyboard attached");
        }
    }

    if (s_pMouse == 0)
    {
        // Through the firmware's bridge and not straight to Circle: the boot
        // menu ran first and claimed the mouse, and CMouseDevice asserts on a
        // second registration with no way to withdraw the first (mouse.cpp:85).
        // The bridge holds that one registration and forwards from here on; it
        // claims the device itself on a boot where the firmware never ran.
        if (FwInputPassMouseTo (MouseStatusHandler))
        {
            s_pMouse = (CMouseDevice *)
                CDeviceNameService::Get ()->GetDevice ("mouse1", FALSE);
#ifdef SHEEPSHAVER
            s_nMouseX = 0;
            s_nMouseY = 0;
            ADBSetRelMouseMode (false);
            CLogger::Get ()->Write (FROM, LogNotice, "Mouse attached, absolute mode");
#else
            ADBSetRelMouseMode (true);
            CLogger::Get ()->Write (FROM, LogNotice, "Mouse attached, relative mode");
#endif
        }
    }
}

void InputInit (void)
{
    memset (s_LastKeys, 0, sizeof s_LastKeys);
    s_LastModifiers = 0;
    s_LastMouseButtons = 0;

    // The Macintosh goes round more than once, and whatever was in the queue
    // belonged to the machine that stopped: an Option held to reach the boot
    // menu must not arrive as an Option pressed in the Finder. Safe to write
    // from here — this runs before the emulator starts, so there is no drain.
    s_nQueueRead = s_nQueueWrite;
    s_nQueueDropped = 0;
#ifdef SHEEPSHAVER
    s_bPostedMove = 0;
#else
    s_nPendingDX = 0;
    s_nPendingDY = 0;
#endif
#ifdef SHEEPSHAVER
    s_nCarryX = 0;
    s_nCarryY = 0;
    s_nLastReportAt = 0;
    int nDpi = (int) PrefsFindInt32 ("mousedpi");
    if (nDpi < 100)  nDpi = 100;
    if (nDpi > 8000) nDpi = 8000;
    s_nMouseDpi = nDpi;
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Pointer: mouse at %d dpi, acceleration from the "
                            "Mouse control panel (the ROM's own curve)", nDpi);
#endif

    InputAttachDevices ();
}

/*
 *  Let go of the devices, so that a later InputInit() attaches them again.
 *
 *  Nothing is unregistered here, and nothing can be: Circle keeps one raw
 *  keyboard handler, and its mouse cannot be given back at all (mouse.cpp:85).
 *  What this clears is only the record of having attached them — the firmware
 *  takes the keyboard by registering over us and the bridge stops forwarding
 *  the mouse, and without this the guards above would refuse to take either
 *  back when the Macintosh starts again.
 */
void InputRelease (void)
{
    s_pKeyboard = 0;
    s_pMouse    = 0;
}
