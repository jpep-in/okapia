//
// kernel.h — Okapia phase 1 smoke test
//
// Does not use circle-stdlib's CStdlibApp helpers: Okapia needs control over
// initialisation order, because the 257 MB Mac RAM block must be allocated before
// any driver claims contiguous space. This is the shape main_circle.cpp will take.
//
// Copyright (C) 2026  Okapia contributors
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/nulldevice.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/bcmframebuffer.h>
#include <circle/serial.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>
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
    void ReportFrameBuffer (void);   // print what the firmware actually granted
    void DrawTestPattern (void);
    bool TestPalette (void);         // indexed modes only
    void ReportStorage (void);
    void AttachInputDevices (void);

    static void KeyPressedHandler (const char *pString);
    static void MouseEventHandler (TMouseEvent Event, unsigned nButtons,
                                   unsigned nPosX, unsigned nPosY,
                                   int nWheelMove);

    // Order matters: these are constructed top to bottom and destroyed bottom up.
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

    // Okapia owns its frame buffer rather than using CScreenDevice: the screen
    // belongs to the emulated Mac, and CScreenDevice bakes its colour depth into
    // libcircle.a at build time, so it cannot give us an indexed mode.
    CBcmFrameBuffer    *m_pFrameBuffer;
    CUSBKeyboardDevice *m_pKeyboard;
    CMouseDevice       *m_pMouse;

    bool m_bScreenAvailable;
    bool m_bStorageAvailable;

    static CKernel *s_pThis;
};

#endif
