//
// kernel.h — Okapia: the Circle kernel that hosts the Macintosh.
//
// Initialisation order is not a matter of taste here:
//   1. serial, so every later failure can say so
//   2. the Mac memory block, before any driver fragments the heap
//   3. drivers, storage, input
//   4. the emulator
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_kernel_h
#define _okapia_kernel_h

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

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CKernel
{
public:
    CKernel (void);
    ~CKernel (void);

    bool Initialize (void);
    TShutdownMode Run (void);

private:
    bool MountStorage (void);
    void SetDefaultPreferences (void);
    bool StartMacintosh (void);

    CActLED            m_ActLED;
    CKernelOptions     m_Options;
    CDeviceNameService m_DeviceNameService;
    CNullDevice        m_NullDevice;
    CExceptionHandler  m_ExceptionHandler;
    CInterruptSystem   m_Interrupt;
    CSerialDevice      m_Serial;
    CTimer             m_Timer;
    CLogger            m_Logger;
    CUSBHCIDevice      m_USBHCI;
    CEMMCDevice        m_EMMC;
    FATFS              m_FileSystem;
    CConsole           m_Console;
};

#endif
