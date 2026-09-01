//
// specimen_kernel.h — a kernel that does nothing but show the interface.
//
// Separate from the Okapia kernel on purpose. It links Circle and the firmware's
// drawing code and nothing else: no Basilisk core, no SD card, no emulator. That
// makes it build in seconds instead of minutes, which is what a design one is
// still deciding needs, and it means looking at the theme costs neither a card
// nor a risk to one.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#ifndef _okapia_specimen_kernel_h
#define _okapia_specimen_kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/types.h>

enum TShutdownMode
{
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};

class CSpecimenKernel
{
public:
    CSpecimenKernel (void);

    bool Initialize (void);
    TShutdownMode Run (void);

private:
    CActLED            m_ActLED;
    CKernelOptions     m_Options;
    CDeviceNameService m_DeviceNameService;
    CExceptionHandler  m_ExceptionHandler;
    CInterruptSystem   m_Interrupt;
    CSerialDevice      m_Serial;
    CTimer             m_Timer;
    CLogger            m_Logger;
};

#endif
