/*
 * hal_circle.h — the board, brought up once, for whoever needs it.
 *
 * Okapia has two kernels already — the Macintosh one and the specimen that
 * shows the firmware with no emulator behind it — and a third is coming for a
 * second engine (docs/project/architecture.md). Each was bringing the board up on its
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
#include <circle/cputhrottle.h>
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

class CSoundBaseDevice;

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
    // (docs/project/architecture.md), so a second real init is a failed assertion and a dead
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
    // (docs/contributing/testing-and-debugging.md). The Macintosh kernel had drifted back to a member; this is
    // the form that was paid for.
    CUSBHCIDevice     *m_pUSBHCI;

    // A pointer for the same reason: its constructor asserts and asks the
    // firmware, and neither may happen before there is a log to say so.
    CCPUThrottle      *m_pCPUThrottle;
};

// The one board, for the life of the image. Built on first use rather than as a
// global, because its constructor blinks the LED and a global would run before
// Circle's own startup has finished.
extern COkapiaBoard &OkapiaBoard (void);

/*
 *  The board's one fine-grained timer
 *
 *  The Mac's vertical blank wants an exact 16625 us, which Circle's periodic
 *  handler cannot give: it fires at HZ, so a tick can only land on a 10 ms grid
 *  and the spacing comes out 20, 10, 20, 20, 10. CUserTimer programs the system
 *  timer's compare register directly and takes microseconds.
 *
 *  It lives here, in the shared half, because it is a claim on the hardware and
 *  claims are the thing this port cannot afford to make twice. tick_circle.cpp
 *  is compiled once *per engine*, so a guard local to it is a guard per engine:
 *  switching from one Macintosh to the other would connect ARM_IRQ_TIMER1 a
 *  second time, and interrupt.cpp:145 asserts on exactly that — which halts the
 *  board, and under QEMU ends the session. Measured, after the switch, as a
 *  machine that simply vanished.
 *
 *  So the timer is claimed once and the handler is replaced, on the model of
 *  the one mouse registration and the one frame buffer. The deadline is kept
 *  here too because it is the same arithmetic for both Macintoshes: rearmed
 *  against an absolute time, and a tick that is already late is dropped rather
 *  than repaid — repaying it hands the guest several vertical blanks with no
 *  time between them.
 */
typedef void TBoardTickHandler (void);

// Answers false where there is no such timer to claim, and the caller then
// falls back on Circle's periodic handler. A second call only swaps the
// handler; the timer and its interrupt are taken once for the life of the board.
bool BoardFineTick (unsigned nPeriodUsec, TBoardTickHandler *pHandler);

/*
 *  What the ARM is actually running at
 *
 *  Start() sets the processor to its maximum rate, and says what the firmware
 *  had left it at. That is not the end of the story on a board that heats: the
 *  firmware caps the rate on its own at 80 °C and throttles at 85, and nothing
 *  reaches the Macintosh to say so — a slow benchmark then reads as a slow
 *  emulator. This asks the firmware and logs whenever the throttling state
 *  changes; with bReport, it also logs the rate and the temperature.
 *
 *  Every few seconds from an engine's own periodic seam, never from an
 *  interrupt: each question is a round trip to the VideoCore that blocks.
 */
void BoardWatchClock (bool bReport);

/*
 *  The log, on the card
 *
 *  The serial port is only a log for whoever has a 3.3 V adapter on GPIO 14
 *  and 15; everyone else has a card. StartCard() opens okapia.log at the root
 *  of it, after keeping the last session's as okapia-previous.log, and this
 *  writes out whatever the logger has gathered since the last call.
 *
 *  Both files have a fixed size, 512 KB, made once, and are overwritten in
 *  place: every write lands in whole sectors the file already owns, and neither
 *  the FAT nor a directory is written again after the files exist. That is the
 *  disk image's own rule (docs/topics/storage.md), and it means a plug pulled during a
 *  flush can cost the last lines of the log and nothing else on the card. A
 *  session longer than the file starts again at its top, below a marker.
 *
 *  From an engine's periodic seam every few seconds, and before the board
 *  halts or reboots — never from an interrupt: it blocks on the card.
 */
void BoardLogFlush (void);

/*
 *  The sound device, claimed once for the life of the board
 *
 *  Circle's sound devices cannot be given back while they play. Cancel() only
 *  asks the DMA to stop at the end of its buffer, and deleting the device
 *  before that happened trips CHDMISoundBaseDevice's destructor assertion
 *  (hdmisoundbasedevice.cpp:166), or frees a PWM device its interrupt still
 *  calls into. A Circle assertion halts, and AudioExit() runs inside ExitAll()
 *  *before* DiskExit(): every restart from Mac OS with sound on could freeze
 *  the board with the disk image still open. And audio_circle.cpp is compiled
 *  once per engine, so a device per engine would be two claims on one socket.
 *
 *  So the device is the board's, 44.1 kHz 16-bit stereo, and every start of
 *  either Macintosh is handed the same one. pWhere is "hdmi", "usb" or "jack".
 *  Asking for another output — the boot menu changed it — stops the device in
 *  hand, waits until it has, and only then replaces it: call it between two
 *  starts, never while a Macintosh is feeding the old one. Answers 0 when the
 *  device will not start, and keeps answering 0 for that output rather than
 *  trying again.
 */
CSoundBaseDevice *BoardSoundClaim (const char *pWhere, unsigned nQueueMsecs);

#endif
