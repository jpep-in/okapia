/*
 * hal_circle.h — the board, brought up once, for whoever needs it.
 *
 * Okapia has two kernels already — the Macintosh one and the specimen that
 * shows the firmware with no emulator behind it — and a third is coming for a
 * second engine (planification.md §19.9). Each was bringing the board up on its
 * own, in the same order, with the same comments, and they had already drifted:
 * one built the USB host as a member and the other as a pointer, which is not a
 * matter of taste but the difference between a working machine and a kernel
 * that hangs with no output at all. This is the one place that knows how.
 *
 * What it owns is what a Raspberry Pi has to be told before anything else can
 * run: the serial port, the log, the interrupt controller, the timer and its
 * clock, the console's standard streams, the USB host, and the card. Nothing
 * above it — not the emulator, not the firmware — should construct any of them.
 *
 * It deliberately does *not* own the frame buffer, the mouse or the periodic
 * timer slot. Those are claimed once for the life of the board by whoever needs
 * them first and handed on afterwards (FwOutputClaim, okapia_input.cpp,
 * TickInit), because the Macintosh may go round more than once and a claim per
 * start leaks or asserts. Bringing them here would say the opposite.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_HAL_CIRCLE_H
#define OKAPIA_HAL_CIRCLE_H

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/input/console.h>
#include <SDCard/emmc.h>
#include <wrap_fatfs.h>
#include <circle/types.h>

class COkapiaBoard
{
public:
    COkapiaBoard (void);
    ~COkapiaBoard (void);

    // Serial, the log, interrupts, the timer and the clock — in that order,
    // which is not negotiable. False means the machine could not be told to
    // speak, and nothing after it would have been able to say why.
    //
    // The caller passes the name it wants in the log, because "okapia" and
    // "specimen" are different programs and a log that cannot tell them apart
    // is a log read wrong.
    bool Start (const char *pName);

    // The console on the serial port — the screen belongs to the Macintosh.
    // Call it before opening any file: CGlueStdioInit is what claims descriptors
    // 0 to 2, and a file opened first lands on stdin's slot.
    bool StartConsole (void);

    // Warns rather than failing. A machine with no keyboard still boots, and a
    // specimen with no input is still worth looking at.
    bool StartUSB (void);

    // The card, mounted as FatFs "SD:". Everything the Macintosh reads and
    // writes is behind this, so a false here is the end of the road.
    bool StartCard (void);

private:
    // Each of the four is idempotent: a second call answers what the first
    // answered and touches nothing. That is what lets one board serve two
    // emulators in the same image — the engine that takes over calls the same
    // Start* sequence and must not re-register a thing. Circle keeps four
    // periodic timer slots and one mouse claim, and neither can be given back
    // (AGENTS.md), so a second real init is a failed assertion and a dead
    // board rather than a bug you get to read about.
    bool               m_bStarted, m_bConsole, m_bUSB, m_bCard;

    CActLED            m_ActLED;
    CKernelOptions     m_Options;
    CDeviceNameService m_DeviceNameService;
    CNullDevice        m_NullDevice;
    CExceptionHandler  m_ExceptionHandler;
    CInterruptSystem   m_Interrupt;
    CSerialDevice      m_Serial;
    CTimer             m_Timer;
    CLogger            m_Logger;
    CConsole           m_Console;
    CEMMCDevice        m_EMMC;
    FATFS              m_FileSystem;

    // A pointer and not a member, so that it is built *after* the log exists.
    // A failed assertion in a member constructor runs before serial and is
    // therefore a silent hang with nothing at all on the wire — which is
    // exactly what the USB host cost when it was written the obvious way
    // (AGENTS.md). The Macintosh kernel had drifted back to a member; this is
    // the form that was paid for.
    CUSBHCIDevice     *m_pUSBHCI;
};

// The one board, for the life of the image. Built on first use rather than as a
// global, because its constructor blinks the LED and a global would run before
// Circle's own startup has finished.
extern COkapiaBoard &OkapiaBoard (void);

#endif
