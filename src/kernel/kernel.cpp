//
// kernel.cpp — Okapia: bring up the board, then hand it to the Macintosh.
//
// Copyright (C) 2026  Okapia contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
#include "kernel.h"
#include <circle_glue.h>
#include <circle/memory.h>
#include <stdio.h>

// Basilisk II headers. sysdeps.h must come first, as everywhere in that tree.
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "xpram.h"

#define FROM "okapia"

// From src/circle/main_circle.cpp
extern bool MacMemoryAllocate (uint32 nRAMSize);
extern bool MacROMLoad (const char *pFileName);
extern bool QuitRequested (void);

// From src/circle/tick_circle.cpp
extern void TickInit (void);
extern void TickStart (void);
extern void TickStop (void);

// From src/circle/input_circle.cpp
extern void InputInit (void);

static const char *ROM_PATH  = "/okapia.rom";
static const char *DISK_PATH = "/machd76.image";
static const uint32 MAC_RAM  = 256 * 1024 * 1024;

CKernel::CKernel (void)
:   m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_USBHCI (&m_Interrupt, &m_Timer, TRUE),
    m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
    m_Console (&m_Serial, &m_Serial)     // both non-null: see AGENTS.md
{
}

CKernel::~CKernel (void)
{
}

bool CKernel::Initialize (void)
{
    // 1. Serial first. A failure before this point is indistinguishable from a hang.
    if (!m_Serial.Initialize (115200))
    {
        return false;
    }
    if (!m_Logger.Initialize (&m_Serial))
    {
        return false;
    }

    m_Logger.Write (FROM, LogNotice, "Okapia, built " __DATE__ " " __TIME__);

    if (!m_Interrupt.Initialize () || !m_Timer.Initialize ())
    {
        m_Logger.Write (FROM, LogError, "Interrupt or timer failed");
        return false;
    }

    // 2. The Mac memory block, before any driver allocates. Circle serves large
    //    blocks by walking forward through free space, so this must come first.
    m_Logger.Write (FROM, LogNotice, "Free before Mac RAM: %u MB low, %u MB high",
                    (unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_LOW) / (1024*1024)),
                    (unsigned) (CMemorySystem::Get ()->GetHeapFreeSpace (HEAP_ANY) / (1024*1024)));

    if (!MacMemoryAllocate (MAC_RAM))
    {
        return false;
    }

    // 3. Everything else.
    if (!m_EMMC.Initialize ())
    {
        m_Logger.Write (FROM, LogError, "No SD card");
        return false;
    }
    if (!MountStorage ())
    {
        return false;
    }
    if (!m_USBHCI.Initialize ())
    {
        m_Logger.Write (FROM, LogWarning, "No USB: keyboard and mouse unavailable");
    }
    if (m_Console.Initialize ())
    {
        CGlueStdioInit (m_Console);
    }

    return true;
}

bool CKernel::MountStorage (void)
{
    if (f_mount (&m_FileSystem, "SD:", 1) != FR_OK)
    {
        m_Logger.Write (FROM, LogError, "Cannot mount the SD card");
        return false;
    }
    return true;
}

/*
 *  Preferences
 *
 *  Hard-coded for now. The plan has these coming from a preferences file on the
 *  card (§8, reusing prefs_unix.cpp) and being editable from the firmware
 *  (§7.12); neither exists yet, and guessing wrong here is cheap to fix.
 */

void CKernel::SetDefaultPreferences (void)
{
    // PrefsInit wants argc/argv by reference; there is no command line here.
    int argc = 0;
    char **argv = 0;
    PrefsInit (0, argc, argv);

    PrefsReplaceInt32 ("ramsize", (int32) MAC_RAM);

    // Upstream defaults frameskip to 6, i.e. a 10 Hz screen. Compositing every
    // VBL costs about a tenth of wall time here, and the Mac draws its own
    // cursor, so a decimated screen reads as a laggy mouse.
    PrefsReplaceInt32 ("frameskip", 0);   // 0 = Dynamic
    PrefsReplaceString ("rom", ROM_PATH);
    PrefsAddString ("disk", DISK_PATH);

    // 14 = Quadra 900. Required for Mac OS 8.x; see plan §7.11.
    PrefsReplaceInt32 ("modelid", 14);
    PrefsReplaceInt32 ("cpu", 4);           // 68040
    PrefsReplaceBool ("fpu", true);         // a 68040 always has one
#ifdef CIRCLE_QEMU
    // QEMU's raspi3b models no sound output at all. Claiming a sound device the
    // Mac cannot actually hear is worse than silence: it froze the guest once,
    // and the hard stop that followed cost a card. Force it on with
    // OKAPIA_FORCE_SOUND if you want to exercise the plumbing anyway.
  #ifdef OKAPIA_FORCE_SOUND
    PrefsReplaceBool ("nosound", false);
  #else
    PrefsReplaceBool ("nosound", true);
  #endif
#else
    PrefsReplaceBool ("nosound", false);
#endif
    PrefsReplaceBool ("nonet", true);       // networking comes later

    // No floppy drives. Without an explicit entry, SonyInit calls
    // SysAddFloppyPrefs(), whose fallback branch adds the Linux /dev/fd0 and
    // /dev/fd1 — phantom drives the Mac then tries, and fails, to mount.
    PrefsAddString ("floppy", "");
}

/*
 *  Hand over to the Macintosh.
 */

bool CKernel::StartMacintosh (void)
{
    SetDefaultPreferences ();

    if (!MacROMLoad (ROM_PATH))
    {
        return false;
    }

    // Prove the disk image is reachable before handing it to the emulator: a
    // Mac that cannot find a boot volume looks the same as one that hangs.
    FILE *pDisk = fopen (DISK_PATH, "rb");
    if (pDisk == 0)
    {
        m_Logger.Write (FROM, LogError, "Disk image not found: %s", DISK_PATH);
        return false;
    }
    char Header[64];
    size_t nRead = fread (Header, 1, sizeof Header, pDisk);
    fseek (pDisk, 1024, SEEK_SET);
    char MDB[8];
    size_t nMDB = fread (MDB, 1, sizeof MDB, pDisk);
    fclose (pDisk);
    m_Logger.Write (FROM, LogNotice,
                    "Disk %s: readable, MDB signature %02X%02X %s",
                    DISK_PATH,
                    nMDB >= 2 ? (unsigned char) MDB[0] : 0,
                    nMDB >= 2 ? (unsigned char) MDB[1] : 0,
                    (nMDB >= 2 && MDB[0] == 'B' && MDB[1] == 'D') ? "(HFS)" : "(unexpected)");
    (void) nRead;

    // Exercise the exact path DiskInit will take, so a failure names itself
    // instead of surfacing as a Mac that never finds a boot volume.
    {
        extern void *Sys_open (const char *name, bool read_only, bool is_cdrom);
        extern void Sys_close (void *fh);
        extern loff_t SysGetFileSize (void *fh);

        void *fh = Sys_open (DISK_PATH, false, false);
        if (fh == 0)
        {
            m_Logger.Write (FROM, LogError, "Sys_open refused %s", DISK_PATH);
        }
        else
        {
            m_Logger.Write (FROM, LogNotice, "Sys_open ok, %lu MB",
                            (unsigned long) (SysGetFileSize (fh) / (1024 * 1024)));

            // Exercise the read path itself: block 0, then the HFS Master
            // Directory Block at 1024. A readable MDB says the file layer and
            // the image agree; it does not say the volume is consistent, which
            // only fsck_hfs on the host can tell you.
            extern size_t Sys_read (void *fh, void *buffer, loff_t offset, size_t length);
            static char Buf[512];

            size_t n0 = Sys_read (fh, Buf, 0, 512);
            m_Logger.Write (FROM, LogNotice, "Sys_read(0, 512) -> %u", (unsigned) n0);

            size_t n1 = Sys_read (fh, Buf, 1024, 512);
            m_Logger.Write (FROM, LogNotice,
                            "Sys_read(1024, 512) -> %u, signature %02X%02X",
                            (unsigned) n1, (unsigned char) Buf[0], (unsigned char) Buf[1]);

            // And far into the file, where a 32-bit offset would break.
            size_t n2 = Sys_read (fh, Buf, 400u * 1024 * 1024, 512);
            m_Logger.Write (FROM, LogNotice, "Sys_read(400MB, 512) -> %u", (unsigned) n2);

            // Two more reads turn "the Mac shows a floppy" into a diagnosis:
            // a volume still marked in use is refused by MountVol (badMDBErr),
            // and nothing downstream explains why.
            extern bool HfsInspect (void *fh, const char *pName);
            extern bool HfsRepair (const char *pPath);

            bool bClean = HfsInspect (fh, DISK_PATH);
            Sys_close (fh);

            // A volume the last session left mounted is refused by MountVol,
            // so repair it before the Mac ever sees it. libhfs scavenges on
            // mount and marks the volume clean on unmount — the same thing a
            // second System would do, without needing one.
            if (!bClean)
            {
                HfsRepair (DISK_PATH);
            }
        }
    }

    m_Logger.Write (FROM, LogNotice, "Initialising the emulator");
    if (!InitAll (0))
    {
        m_Logger.Write (FROM, LogError, "InitAll failed");
        return false;
    }

    m_Logger.Write (FROM, LogNotice, "Mac RAM at %p (Mac 0x%08X), ROM at %p (Mac 0x%08X)",
                    RAMBaseHost, (unsigned) RAMBaseMac,
                    ROMBaseHost, (unsigned) ROMBaseMac);
    m_Logger.Write (FROM, LogNotice, "CPU type %d, FPU %d, 24-bit addressing %s",
                    CPUType, FPUType, TwentyFourBitAddressing ? "on" : "off");

    // USB devices are attached once, before the 68k loop takes over. Circle's
    // plug-and-play needs polling from core 0, which is not available while
    // Start680x0() runs; hot-plug will come with the multicore split (§7.3).
    InputInit ();

    // The heartbeat, armed only now: everything it pokes must already exist.
    TickInit ();
    TickStart ();

    m_Logger.Write (FROM, LogNotice, "Entering 68k execution");
    Start680x0 ();
    TickStop ();                          // does not return until the Mac stops

    m_Logger.Write (FROM, LogNotice, "68k execution ended");
    return true;
}

TShutdownMode CKernel::Run (void)
{
    if (!StartMacintosh ())
    {
        m_Logger.Write (FROM, LogError, "The Macintosh did not start");
        // Stay alive rather than reboot: the serial log is the only diagnosis.
        for (unsigned i = 0; i < 10; i++)
        {
            CTimer::Get ()->MsDelay (1000);
        }
        return ShutdownHalt;
    }

    // ExitAll() closes the drivers in order — DiskExit() is what finally calls
    // Sys_close() on the disk image, flushing FatFs to the card. It saves the
    // PRAM itself, so no separate XPRAMExit() here.
    ExitAll ();

    m_Logger.Write (FROM, LogNotice, "Shut down cleanly, disk closed");

    return QuitRequested () ? ShutdownHalt : ShutdownReboot;
}
