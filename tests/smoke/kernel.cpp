//
// kernel.cpp — Okapia phase 1 smoke test
//
// Proves the bare-metal environment before any Macintosh code exists: serial log,
// frame buffer, palette, timer, USB keyboard and mouse, SD card and FAT.
//
// It reports what the firmware ACTUALLY granted rather than what was requested.
// On the Raspberry Pi 5 the requested resolution is ignored outright, so a test
// that echoes its own request would be worse than no test at all.
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
#include "kernel.h"
#include "exctest.h"
#include <circle_glue.h>
#include <stdio.h>
#include <dirent.h>
#include <circle/bcmframebuffer.h>
#include <circle/devicenameservice.h>
#include <circle/string.h>
#include <circle/util.h>

#define FROM_KERNEL "smoke"

CKernel *CKernel::s_pThis = 0;

CKernel::CKernel (void)
:   m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_USBHCI (&m_Interrupt, &m_Timer, TRUE),          // TRUE: plug and play
    m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
    m_Console (&m_Serial, &m_Serial),
    m_pFrameBuffer (0),
    m_pKeyboard (0),
    m_pMouse (0),
    m_bScreenAvailable (false),
    m_bStorageAvailable (false),
    m_bStdioAvailable (false)
{
    s_pThis = this;
    // Nothing here. CActLED drives the activity LED through the mailbox virtual
    // GPIO on the Pi 3, which QEMU does not implement: blinking it in the
    // constructor hangs the kernel before the serial log exists.
}

CKernel::~CKernel (void)
{
    s_pThis = 0;
}

//
// Initialisation, in the order Circle requires. Okapia will insert the Mac RAM
// allocation at the very top of this sequence, before any driver runs.
//

bool CKernel::Initialize (void)
{
    // Serial first, always. Every later failure has to be able to say so; a
    // headless board that dies before the log exists tells us nothing at all.
    if (!m_Serial.Initialize (115200))
    {
        return false;
    }

    if (!m_Logger.Initialize (&m_Serial))
    {
        return false;
    }

    m_Logger.Write (FROM_KERNEL, LogNotice,
                    "Okapia smoke test, built " __DATE__ " " __TIME__);

    if (!m_Interrupt.Initialize ())
    {
        m_Logger.Write (FROM_KERNEL, LogError, "Interrupt system failed");
        return false;
    }

    if (!m_Timer.Initialize ())
    {
        m_Logger.Write (FROM_KERNEL, LogError, "Timer failed");
        return false;
    }

    // Ask for the mode Mac OS actually uses. What matters is what comes back:
    // on the Raspberry Pi 5 the request is ignored outright.
    m_pFrameBuffer = new CBcmFrameBuffer (640, 480, 8);
    m_bScreenAvailable = m_pFrameBuffer->Initialize ();
    if (!m_bScreenAvailable)
    {
        m_Logger.Write (FROM_KERNEL, LogWarning,
                        "Frame buffer 640x480x8 refused: running headless");
        delete m_pFrameBuffer;
        m_pFrameBuffer = 0;
    }

    if (!m_EMMC.Initialize ())
    {
        m_Logger.Write (FROM_KERNEL, LogWarning,
                        "No SD card: storage tests will be skipped");
    }
    else if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
    {
        m_Logger.Write (FROM_KERNEL, LogWarning,
                        "SD card present but SD: will not mount");
    }
    else
    {
        m_bStorageAvailable = true;
    }

    if (!m_USBHCI.Initialize ())
    {
        m_Logger.Write (FROM_KERNEL, LogWarning, "No USB: input tests skipped");
    }

    if (!m_Console.Initialize ())
    {
        m_Logger.Write (FROM_KERNEL, LogWarning, "Console failed: no stdio");
    }
    else
    {
        CGlueStdioInit (m_Console);   // newlib stdio onto FatFs and the console
        m_bStdioAvailable = true;
    }

    return true;
}

//
// Frame buffer
//

void CKernel::ReportFrameBuffer (void)
{
    CBcmFrameBuffer *pFB = m_pFrameBuffer;
    if (pFB == 0)
    {
        m_Logger.Write (FROM_KERNEL, LogError, "No frame buffer");
        return;
    }

    // Requested vs granted: on the Pi 5 these differ and the difference matters.
    m_Logger.Write (FROM_KERNEL, LogNotice, "Frame buffer requested 640x480x8");
    m_Logger.Write (FROM_KERNEL, LogNotice,
                   "Frame buffer granted   %ux%u, %u bpp, pitch %u, %u KB at 0x%lX",
                   pFB->GetWidth (), pFB->GetHeight (), pFB->GetDepth (),
                   pFB->GetPitch (), pFB->GetSize () / 1024,
                   (unsigned long) pFB->GetBuffer ());

    const unsigned nExpectedPitch = pFB->GetWidth () * pFB->GetDepth () / 8;
    if (pFB->GetPitch () != nExpectedPitch)
    {
        m_Logger.Write (FROM_KERNEL, LogWarning,
                       "Pitch %u != width*bpp/8 (%u): rows are padded",
                       pFB->GetPitch (), nExpectedPitch);
    }
}

void CKernel::DrawTestPattern (void)
{
    CBcmFrameBuffer *pFB = m_pFrameBuffer;
    if (pFB == 0)
    {
        return;
    }

    const unsigned nWidth  = pFB->GetWidth ();
    const unsigned nHeight = pFB->GetHeight ();
    const unsigned nPitch  = pFB->GetPitch ();
    const unsigned nDepth  = pFB->GetDepth ();
    u8 *pBuffer = (u8 *) (uintptr) pFB->GetBuffer ();

    // Eight vertical bars, plus a one-pixel border. The border is what reveals a
    // wrong pitch: it comes out slanted instead of square.
    for (unsigned y = 0; y < nHeight; y++)
    {
        u8 *pRow = pBuffer + y * nPitch;

        for (unsigned x = 0; x < nWidth; x++)
        {
            const unsigned nBar = x * 8 / nWidth;
            const bool bBorder =    x == 0 || x == nWidth - 1
                                 || y == 0 || y == nHeight - 1;

            const u8 nRed   = bBorder ? 0xFF : ((nBar & 4) ? 0xFF : 0);
            const u8 nGreen = bBorder ? 0xFF : ((nBar & 2) ? 0xFF : 0);
            const u8 nBlue  = bBorder ? 0xFF : ((nBar & 1) ? 0xFF : 0);

            switch (nDepth)
            {
            case 8:
                // Indexed: write the bar number, the palette gives it a colour.
                pRow[x] = bBorder ? 255 : (u8) (nBar * 32);
                break;

            case 16:
                ((u16 *) pRow)[x] = (u16) (  ((nRed   >> 3) << 11)
                                           | ((nGreen >> 2) << 5)
                                           |  (nBlue  >> 3));
                break;

            case 32:
            default:
                ((u32 *) pRow)[x] = (u32) (0xFF000000 | (nRed << 16)
                                           | (nGreen << 8) | nBlue);
                break;
            }
        }
    }

    m_Logger.Write (FROM_KERNEL, LogNotice, "Test pattern drawn (%u bpp)", nDepth);
}

bool CKernel::TestPalette (void)
{
    CBcmFrameBuffer *pFB = m_pFrameBuffer;
    if (pFB == 0 || pFB->GetDepth () > 8)
    {
        m_Logger.Write (FROM_KERNEL, LogNotice,
                       "Palette test skipped: depth is %u bpp, not indexed",
                       pFB != 0 ? pFB->GetDepth () : 0);
        return false;
    }

    // Eight bars of 32 entries each, matching DrawTestPattern's index scheme.
    for (unsigned i = 0; i < 256; i++)
    {
        const unsigned nBar = i / 32;
        pFB->SetPalette32 ((u8) i, (u32) (0xFF000000
                                          | ((nBar & 4) ? 0xFF0000 : 0)
                                          | ((nBar & 2) ? 0x00FF00 : 0)
                                          | ((nBar & 1) ? 0x0000FF : 0)));
    }
    pFB->SetPalette32 (255, 0xFFFFFFFF);        // border

    if (!pFB->UpdatePalette ())
    {
        m_Logger.Write (FROM_KERNEL, LogError, "UpdatePalette failed");
        return false;
    }

    m_Logger.Write (FROM_KERNEL, LogNotice, "Palette test ok (256 entries)");
    return true;
}

//
// Storage
//

void CKernel::ReportStorage (void)
{
    if (!m_bStorageAvailable)
    {
        m_Logger.Write (FROM_KERNEL, LogNotice, "No storage: skipping");
        return;
    }

    FATFS_DIR Dir;
    if (f_opendir (&Dir, "SD:/") != FR_OK)
    {
        m_Logger.Write (FROM_KERNEL, LogError, "Cannot open SD:/");
        return;
    }

    unsigned nEntries = 0;
    FILINFO FileInfo;
    while (f_readdir (&Dir, &FileInfo) == FR_OK && FileInfo.fname[0] != 0)
    {
        if (nEntries < 8)
        {
            m_Logger.Write (FROM_KERNEL, LogNotice, "  %s (%lu bytes)",
                            FileInfo.fname, (unsigned long) FileInfo.fsize);
        }
        nEntries++;
    }
    f_closedir (&Dir);
    m_Logger.Write (FROM_KERNEL, LogNotice, "SD card: %u entries in root", nEntries);

    // Read a known file back: proves the FAT layer, not merely the mount.
    FIL File;
    if (f_open (&File, "SD:/okapia.txt", FA_READ) == FR_OK)
    {
        char Buffer[128];
        UINT nRead = 0;
        if (f_read (&File, Buffer, sizeof Buffer - 1, &nRead) == FR_OK && nRead > 0)
        {
            Buffer[nRead] = '\0';
            char *pNewline = Buffer;
            while (*pNewline != '\0' && *pNewline != '\n') pNewline++;
            *pNewline = '\0';
            m_Logger.Write (FROM_KERNEL, LogNotice, "okapia.txt: %s", Buffer);
        }
        f_close (&File);
    }
    else
    {
        m_Logger.Write (FROM_KERNEL, LogNotice, "okapia.txt absent");
    }
}

void CKernel::ReportStdio (void)
{
    if (!m_bStdioAvailable || !m_bStorageAvailable)
    {
        m_Logger.Write (FROM_KERNEL, LogNotice, "stdio unavailable: skipping");
        return;
    }

    // This is the layer that makes extfs_unix.cpp and prefs_unix.cpp reusable
    // almost as-is, so it is worth proving early rather than at phase 3.
    FILE *pFile = fopen ("/okapia.txt", "r");
    if (pFile == 0)
    {
        m_Logger.Write (FROM_KERNEL, LogError, "stdio: fopen failed");
        return;
    }
    char Buffer[128];
    if (fgets (Buffer, sizeof Buffer, pFile) != 0)
    {
        char *p = Buffer;
        while (*p != '\0' && *p != '\n') p++;
        *p = '\0';
        m_Logger.Write (FROM_KERNEL, LogNotice, "stdio fopen/fgets: %s", Buffer);
    }
    fclose (pFile);

    DIR *pDir = opendir ("/");
    if (pDir == 0)
    {
        m_Logger.Write (FROM_KERNEL, LogError, "stdio: opendir failed");
        return;
    }
    unsigned nEntries = 0;
    while (readdir (pDir) != 0)
    {
        nEntries++;
    }
    closedir (pDir);
    m_Logger.Write (FROM_KERNEL, LogNotice,
                    "stdio opendir/readdir: %u entries", nEntries);
}

//
// Input
//

void CKernel::KeyPressedHandler (const char *pString)
{
    if (s_pThis != 0)
    {
        s_pThis->m_Logger.Write (FROM_KERNEL, LogNotice, "Key: '%s'", pString);
    }
}

void CKernel::MouseEventHandler (TMouseEvent Event, unsigned nButtons,
                                 unsigned nPosX, unsigned nPosY, int nWheelMove)
{
    if (s_pThis == 0 || Event != MouseEventMouseMove)
    {
        if (s_pThis != 0 && Event != MouseEventMouseMove)
        {
            s_pThis->m_Logger.Write (FROM_KERNEL, LogNotice,
                                    "Mouse event %u, buttons %u at %u,%u",
                                    (unsigned) Event, nButtons, nPosX, nPosY);
        }
        return;
    }
}

void CKernel::AttachInputDevices (void)
{
    if (m_pKeyboard == 0)
    {
        m_pKeyboard = (CUSBKeyboardDevice *)
            CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
        if (m_pKeyboard != 0)
        {
            m_pKeyboard->RegisterKeyPressedHandler (KeyPressedHandler);
            m_Logger.Write (FROM_KERNEL, LogNotice, "Keyboard attached");
        }
    }

    if (m_pMouse == 0)
    {
        m_pMouse = (CMouseDevice *)
            CDeviceNameService::Get ()->GetDevice ("mouse1", FALSE);
        if (m_pMouse != 0)
        {
            m_pMouse->RegisterEventHandler (MouseEventHandler);
            m_Logger.Write (FROM_KERNEL, LogNotice, "Mouse attached");
        }
    }
}

//
// Main
//

TShutdownMode CKernel::Run (void)
{
    m_Logger.Write (FROM_KERNEL, LogNotice, "Okapia smoke test");
    m_Logger.Write (FROM_KERNEL, LogNotice, "Raspberry Pi %u, AArch%u",
                   (unsigned) RASPPI, (unsigned) AARCH);

    ReportFrameBuffer ();
    TestPalette ();
    DrawTestPattern ();
    ReportStorage ();
    ReportStdio ();

    // Decides which exception mechanism Okapia asks Basilisk to use.
    const int nCaught = TestCppException ();
    if (nCaught == 2)
    {
        m_Logger.Write (FROM_KERNEL, LogNotice,
                        "C++ exceptions work: threw and caught across 4 frames");
    }
    else
    {
        m_Logger.Write (FROM_KERNEL, LogError,
                        "C++ exceptions broken (got %d): use EXCEPTIONS_VIA_LONGJMP",
                        nCaught);
    }

    m_Logger.Write (FROM_KERNEL, LogNotice,
                   "Running. Press keys or move the mouse; halts after 30 s.");

    // One line per second: a silent console would not distinguish "running" from
    // "hung". Plug and play is polled here, so devices may appear mid-run.
    const unsigned nSeconds = 30;
    for (unsigned i = 0; i < nSeconds; i++)
    {
        m_USBHCI.UpdatePlugAndPlay ();
        AttachInputDevices ();

        CTimer::Get ()->MsDelay (1000);

        if (i % 5 == 0)
        {
            m_Logger.Write (FROM_KERNEL, LogNotice, "  t+%us, uptime %u ms",
                           i, CTimer::Get ()->GetClockTicks () / 1000);
        }
    }

    m_Logger.Write (FROM_KERNEL, LogNotice, "Smoke test complete");

    return ShutdownHalt;
}
