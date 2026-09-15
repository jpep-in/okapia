/*
 * hal_circle.cpp — bringing a Raspberry Pi up, once, in the right order.
 *
 * Every comment here was paid for by a failure, and they are gathered rather
 * than repeated: see hal_circle.h for what this owns and what it deliberately
 * does not.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal_circle.h"

#include <circle/bcmpropertytags.h>
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle_glue.h>

#include <stdio.h>
#include <string.h>

#ifndef OKAPIA_BUILD_TIME
#define OKAPIA_BUILD_TIME 0
#endif

#define FROM "okapia-board"

// Asked of the firmware directly rather than through CCPUThrottle, because the
// point is to read the rate *before* anything has been done to it.
static unsigned ArmClockMHz (void)
{
    CBcmPropertyTags Tags;
    TPropertyTagClockRate Rate;
    Rate.nClockId = CLOCK_ID_ARM;
    if (!Tags.GetTag (PROPTAG_GET_CLOCK_RATE, &Rate, sizeof Rate, 4))
    {
        return 0;
    }
    return Rate.nRate / 1000000;
}

// The log on the card, further down with BoardLogFlush().
static void LogStart (void);

COkapiaBoard::COkapiaBoard (void)
:   m_bStarted (false), m_bConsole (false), m_bUSB (false), m_bCard (false),
    m_Timer (&m_Interrupt),
    m_Logger (m_Options.GetLogLevel (), &m_Timer),
    m_Console (&m_Serial, &m_Serial),    // both non-null: see docs/contributing/testing-and-debugging.md
    m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
    m_pUSBHCI (0),
    m_pCPUThrottle (0)
{
    // On, and not Blink (2): that is 1.4 s of blocking delays (actled.cpp:95)
    // before the serial port, the card or the chime, at every power-on.
    m_ActLED.On ();
}

COkapiaBoard::~COkapiaBoard (void)
{
    // Nothing is given back. A Raspberry Pi that has stopped needing its serial
    // port has stopped, and the one thing a destructor could still do is hide
    // the reason.
}

bool COkapiaBoard::Start (const char *pName)
{
    if (m_bStarted)
    {
        return true;
    }
    // Serial first, always. A failure before this point cannot be told from a
    // hang, because there is nothing yet that could say so.
    if (!m_Serial.Initialize (115200))
    {
        return false;
    }
    m_bStarted = true;
    if (!m_Logger.Initialize (&m_Serial))
    {
        return false;
    }

    m_Logger.Write (pName, LogNotice, "Okapia, built " __DATE__ " " __TIME__);

    if (!m_Interrupt.Initialize () || !m_Timer.Initialize ())
    {
        m_Logger.Write (pName, LogError, "Interrupt or timer failed");
        return false;
    }

    // The one place the clock is set. Everything that asks the time afterwards
    // reads Circle's — the Mac through TimerDateTime(), FatFs through
    // get_fattime() — so there is a single source and no second opinion.
    //
    // That source is the build time, because there is no other yet: a Pi has no
    // RTC and NTP needs the network (phase 10). Left at zero, Circle counts
    // from 1970 and FatFs stamps every file 1980-00-00, which is not even a
    // valid date; the Mac stamps volumes with 1904 dates, leaving drLsMod
    // earlier than drCrDate and an MDB fsck_hfs calls damaged. So the clock is
    // wrong by however long ago this was built, but it is ordered and
    // plausible, which is what file systems care about.
    if (!m_Timer.SetTime (OKAPIA_BUILD_TIME, FALSE))
    {
        m_Logger.Write (pName, LogWarning,
                        "Could not set the clock; dates will start at 1970");
    }

    // With no operating system to ask for more, the firmware leaves the ARM at
    // the bottom of its range — arm_freq_min, 600 MHz on a Pi 4 against 1500 —
    // and the first benchmarks on a real board were taken like that. The rate
    // is logged before it is changed, because it is the one number that says
    // what every measurement before this line existed was worth.
    //
    // Maximum, and deliberately not SetOnTemperature(): that one enforces
    // socmaxtemp, whose default is 60 °C (circle/doc/cmdline.txt), and a Pi 4
    // under load reaches it in minutes and drops back to the speed this is
    // here to leave. The firmware still protects the chip on its own, and
    // BoardWatchClock() says when it does.
    const unsigned nLeftAt = ArmClockMHz ();
    m_pCPUThrottle = new CCPUThrottle (CPUSpeedMaximum);
    const unsigned nTemp = m_pCPUThrottle->GetTemperature ();
    if (m_pCPUThrottle->IsDynamic ())
    {
        m_Logger.Write (FROM, LogNotice,
                        "ARM clock: %u MHz as the firmware left it, now %u MHz "
                        "(range %u to %u), SoC at %u C",
                        nLeftAt, m_pCPUThrottle->GetClockRate () / 1000000,
                        m_pCPUThrottle->GetMinClockRate () / 1000000,
                        m_pCPUThrottle->GetMaxClockRate () / 1000000, nTemp);
    }
    else if (nLeftAt != 0)
    {
        // force_turbo=1, or a firmware that offers a single rate.
        m_Logger.Write (FROM, LogNotice,
                        "ARM clock: %u MHz, fixed by the firmware, SoC at %u C",
                        nLeftAt, nTemp);
    }
    else
    {
        // QEMU: its machines answer none of these questions.
        m_Logger.Write (FROM, LogNotice,
                        "ARM clock: not reported by the firmware");
    }

    return true;
}

bool COkapiaBoard::StartConsole (void)
{
    if (m_bConsole)
    {
        return true;
    }
    // The console goes on the serial port: the screen belongs to the Macintosh.
    // Before any file is opened, because CGlueStdioInit is what claims file
    // descriptors 0 to 2 — open a file first and it lands on stdin's slot.
    if (!m_Console.Initialize ())
    {
        return false;
    }
    CGlueStdioInit (m_Console);
    m_bConsole = true;
    return true;
}

bool COkapiaBoard::StartUSB (void)
{
    if (m_bUSB)
    {
        return true;
    }
    m_pUSBHCI = new CUSBHCIDevice (&m_Interrupt, &m_Timer, TRUE);
    if (m_pUSBHCI == 0 || !m_pUSBHCI->Initialize ())
    {
        return false;
    }
    m_bUSB = true;
    return true;
}

bool COkapiaBoard::StartCard (void)
{
    if (m_bCard)
    {
        return true;
    }
    if (!m_EMMC.Initialize ())
    {
        return false;
    }
    // "SD:" is the name the rest of Okapia opens files under, and the 1 asks
    // FatFs to mount now rather than on first access — so a card that cannot be
    // read says so here instead of from inside something else.
    m_bCard = f_mount (&m_FileSystem, "SD:", 1) == FR_OK;

    // Said because it decides what a disk image costs. FatFs keeps no map of a
    // file's clusters, so every seek inside an image follows the chain one
    // cluster at a time and reads a FAT sector every 128 of them, and every
    // read stops at a cluster boundary. A 500 MB image on 4 KB clusters is a
    // chain of 128 000 links; on 32 KB clusters it is 16 000.
    if (m_bCard)
    {
        static const char *const Types[] = {"?", "FAT12", "FAT16", "FAT32", "exFAT"};
        const unsigned nType = m_FileSystem.fs_type < 5 ? m_FileSystem.fs_type : 0;
        const unsigned nClusterBytes = m_FileSystem.csize * FF_MIN_SS;
        m_Logger.Write (FROM, nClusterBytes < 16384 ? LogWarning : LogNotice,
                        "Card: %s, %u-byte clusters%s", Types[nType], nClusterBytes,
                        nClusterBytes < 16384 ? "; seeks inside a disk image walk "
                                                "the FAT a cluster at a time, and "
                                                "32 KB walks far shorter" : "");
        LogStart ();
    }
    return m_bCard;
}

/*
 *  The log, on the card. See hal_circle.h.
 */

#define LOG_PATH          "SD:/okapia.log"
#define LOG_PREVIOUS_PATH "SD:/okapia-previous.log"

static const u32 LOG_SECTOR = 512;
// Every write is whole chunks or whole sectors, and the file is a whole number
// of chunks, so nothing ever lands past its end.
static const u32 LOG_CHUNK  = 32 * 1024;
static const u32 LOG_BYTES  = 16 * LOG_CHUNK;

static const char LOG_WRAPPED[] =
    "\n--- okapia.log is full and starts again here: newer lines are above this"
    " one, older ones below ---\n";

static FIL  s_Log;
static bool s_bLog;
static u32  s_nLogAt;                   // this session's text, in bytes
static char s_LogTail[LOG_SECTOR];      // what the sector s_nLogAt is in holds
static char s_LogChunk[LOG_CHUNK];      // static: 32 KB is no stack frame

// Open a log file at its full size, making it the first time. Making it is the
// one moment the FAT and the directory are written. The file is never closed
// afterwards, deliberately: closing writes the directory entry's date again,
// and nothing about the file needs saying there.
static bool LogFileReady (FIL *pFile, const char *pPath)
{
    if (f_open (pFile, pPath, FA_READ | FA_WRITE | FA_OPEN_EXISTING) == FR_OK)
    {
        if (f_size (pFile) == LOG_BYTES)
        {
            return true;
        }
        f_close (pFile);
    }

    if (f_open (pFile, pPath, FA_READ | FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
    {
        return false;
    }
    memset (s_LogChunk, ' ', LOG_CHUNK);
    UINT nDone;
    for (u32 n = 0; n < LOG_BYTES; n += LOG_CHUNK)
    {
        if (f_write (pFile, s_LogChunk, LOG_CHUNK, &nDone) != FR_OK || nDone != LOG_CHUNK)
        {
            f_close (pFile);
            return false;
        }
    }
    return f_sync (pFile) == FR_OK;
}

static bool LogChunkAt (FIL *pFile, u32 nOffset, bool bWrite)
{
    UINT nDone;
    if (f_lseek (pFile, nOffset) != FR_OK)
    {
        return false;
    }
    const FRESULT Result = bWrite ? f_write (pFile, s_LogChunk, LOG_CHUNK, &nDone)
                                  : f_read  (pFile, s_LogChunk, LOG_CHUNK, &nDone);
    return Result == FR_OK && nDone == LOG_CHUNK;
}

static void LogStart (void)
{
    static FIL Previous;
    if (!LogFileReady (&s_Log, LOG_PATH) || !LogFileReady (&Previous, LOG_PREVIOUS_PATH))
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "No log on the card: cannot make %s", LOG_PATH);
        return;
    }

    // The last session becomes the previous one — unless it wrote nothing,
    // which keeps a card that was only ever booted once from losing the one
    // log it has.
    bool bOK = LogChunkAt (&s_Log, 0, false);
    bool bBlank = true;
    for (u32 i = 0; bOK && i < LOG_SECTOR && bBlank; i++)
    {
        bBlank = s_LogChunk[i] == ' ';
    }
    if (bOK && !bBlank)
    {
        for (u32 n = 0; bOK && n < LOG_BYTES; n += LOG_CHUNK)
        {
            bOK = LogChunkAt (&s_Log, n, false) && LogChunkAt (&Previous, n, true);
        }
        memset (s_LogChunk, ' ', LOG_CHUNK);
        for (u32 n = 0; bOK && n < LOG_BYTES; n += LOG_CHUNK)
        {
            bOK = LogChunkAt (&s_Log, n, true);
        }
    }
    if (!bOK)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No log on the card: %s unreadable",
                                LOG_PATH);
        return;
    }

    s_nLogAt = 0;
    s_bLog = true;
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Logging to %s every few seconds; the last session is in %s",
                            LOG_PATH + 3, LOG_PREVIOUS_PATH + 3);
    BoardLogFlush ();
}

void BoardLogFlush (void)
{
    while (s_bLog)
    {
        const u32 nPrefix = s_nLogAt % LOG_SECTOR;
        const u32 nStart  = s_nLogAt - nPrefix;
        u32 nLimit = LOG_BYTES - nStart;
        if (nLimit > LOG_CHUNK)
        {
            nLimit = LOG_CHUNK;
        }
        if (nLimit <= nPrefix)
        {
            // Full: start again at the top, under a line that says so.
            memcpy (s_LogTail, LOG_WRAPPED, sizeof LOG_WRAPPED - 1);
            s_nLogAt = sizeof LOG_WRAPPED - 1;
            continue;
        }

        memcpy (s_LogChunk, s_LogTail, nPrefix);
        const int nNew = CLogger::Get ()->Read (s_LogChunk + nPrefix, nLimit - nPrefix);
        if (nNew <= 0)
        {
            return;
        }

        // Out to the end of the sector, padded with spaces: a sector that is
        // written whole goes to the card as it stands, with nothing held in
        // FatFs's buffer and no directory entry to update afterwards.
        const u32 nUsed  = nPrefix + (u32) nNew;
        const u32 nWhole = (nUsed + LOG_SECTOR - 1) & ~(LOG_SECTOR - 1);
        memset (s_LogChunk + nUsed, ' ', nWhole - nUsed);

        UINT nDone;
        if (   f_lseek (&s_Log, nStart) != FR_OK
            || f_write (&s_Log, s_LogChunk, nWhole, &nDone) != FR_OK
            || nDone != nWhole)
        {
            s_bLog = false;
            CLogger::Get ()->Write (FROM, LogWarning,
                                    "Writing %s failed; no more log on the card", LOG_PATH);
            return;
        }

        s_nLogAt = nStart + nUsed;
        const u32 nTail = nUsed % LOG_SECTOR;
        memcpy (s_LogTail, s_LogChunk + (nUsed - nTail), nTail);

        if ((u32) nNew < nLimit - nPrefix)
        {
            return;                     // the logger has nothing more
        }
    }
}

/*
 *  What the ARM is actually running at. See hal_circle.h.
 */
void BoardWatchClock (bool bReport)
{
    CBcmPropertyTags Tags;
    TPropertyTagSimple Throttled;
    // 0xFFFF also clears the "has occurred" bits, so bits 16 to 19 mean "since
    // the last time anyone asked" — which is exactly the interval between two
    // calls here.
    Throttled.nValue = 0xFFFF;
    if (Tags.GetTag (PROPTAG_GET_THROTTLED, &Throttled, sizeof Throttled, 4))
    {
        static const char *const Names[] = {
            "under-voltage", "frequency capped", "throttled", "soft temperature limit"
        };
        static u32 s_nSaid;
        const u32 nState = Throttled.nValue & 0xF000F;
        if (nState != s_nSaid)
        {
            s_nSaid = nState;
            char Line[160];
            unsigned nAt = 0;
            Line[0] = '\0';
            for (unsigned i = 0; i < 4; i++)
            {
                const bool bNow   = (nState & (1u << i)) != 0;
                const bool bSince = (nState & (1u << (16 + i))) != 0;
                if (bNow || bSince)
                {
                    nAt += (unsigned) snprintf (Line + nAt, sizeof Line - nAt,
                                                "%s%s (%s)", nAt != 0 ? ", " : "",
                                                Names[i], bNow ? "now" : "since last look");
                }
            }
            CLogger::Get ()->Write (FROM, nState != 0 ? LogWarning : LogNotice,
                                    "SoC: %s", nState != 0 ? Line : "no longer throttled");
        }
    }

    CCPUThrottle *pThrottle = CCPUThrottle::Get ();
    if (!bReport || pThrottle == 0)
    {
        return;
    }
    const unsigned nRate = pThrottle->GetClockRate ();
    if (nRate != 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "ARM clock %u MHz, SoC at %u C",
                                nRate / 1000000, pThrottle->GetTemperature ());
    }
}

/*
 *  The one fine timer. See hal_circle.h for why it lives on this side.
 */
#if RASPPI <= 4

#include <circle/usertimer.h>
#include <circle/interrupt.h>
#include <circle/timer.h>

static CUserTimer          *s_pFineTimer;
static TBoardTickHandler   *s_pTickHandler;
static unsigned             s_nPeriodUsec = 16625;
static unsigned             s_nNextDue;

// Never wake sooner than this: CUserTimer::Start asserts on a delay of 1 or
// less, and a deadline already past has to become a fresh one rather than a
// storm of immediate interrupts.
static const unsigned MIN_DELAY_USEC = 200;

static void FineTickHandler (CUserTimer *pTimer, void *pParam)
{
    (void) pParam;

    // Rearmed before the work, so a slow tick delays this one and not the next.
    const unsigned nNow = CTimer::GetClockTicks ();
    s_nNextDue += s_nPeriodUsec;
    int nDelay = (int) (s_nNextDue - nNow);
    if (nDelay < (int) MIN_DELAY_USEC)
    {
        // Late. Resynchronise, never repay: upstream's own thread has the same
        // rule (main_unix.cpp:1348), and repaying turned one delay into sixteen
        // ticks at 0 ms followed by a 140 ms hole.
        s_nNextDue = nNow + s_nPeriodUsec;
        nDelay = (int) s_nPeriodUsec;
    }
    pTimer->Start ((unsigned) nDelay);

    TBoardTickHandler *pHandler = s_pTickHandler;
    if (pHandler != 0)
    {
        (*pHandler) ();
    }
}

bool BoardFineTick (unsigned nPeriodUsec, TBoardTickHandler *pHandler)
{
    s_nPeriodUsec  = nPeriodUsec;
    s_pTickHandler = pHandler;

    if (s_pFineTimer != 0)
    {
        // The second Macintosh of a session arrives here. Said once, because
        // this is the line that will show the engine switch was survived: the
        // claim before it connected ARM_IRQ_TIMER1, and interrupt.cpp:145
        // asserts on a second connection — which halts the board.
        static bool s_bSaid;
        if (!s_bSaid)
        {
            s_bSaid = true;
            CLogger::Get ()->Write ("okapia-board", LogNotice,
                                    "Fine timer already claimed; handler swapped "
                                    "for the other engine");
        }
        return true;
    }

    s_pFineTimer = new CUserTimer (CInterruptSystem::Get (), FineTickHandler);
    if (s_pFineTimer == 0 || !s_pFineTimer->Initialize ())
    {
        delete s_pFineTimer;
        s_pFineTimer = 0;
        s_pTickHandler = 0;
        return false;
    }
    s_nNextDue = CTimer::GetClockTicks () + nPeriodUsec;
    s_pFineTimer->Start (nPeriodUsec);
    return true;
}

#else

// A Pi 5 is not assumed to have it: nothing here reaches for that board's
// hardware without having seen it work.
bool BoardFineTick (unsigned, TBoardTickHandler *)
{
    return false;
}

#endif

COkapiaBoard &OkapiaBoard (void)
{
    // Function-local, so it is constructed the first time somebody asks and not
    // before Circle's startup has run. Never destroyed: see the destructor.
    static COkapiaBoard s_Board;
    return s_Board;
}

/*
 *  The sound device — see hal_circle.h
 */

#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#if RASPPI >= 4
#include <circle/sound/usbsoundbasedevice.h>
#endif

#include "okapia_output.h"

static const unsigned SOUND_SAMPLE_RATE = 44100;

static CSoundBaseDevice *s_pBoardSound;
static char              s_BoardSoundWhere[8];
static bool              s_bBoardSoundTried;

CSoundBaseDevice *BoardSoundClaim (const char *pWhere, unsigned nQueueMsecs)
{
    if (s_bBoardSoundTried)
    {
        if (strcmp (pWhere, s_BoardSoundWhere) == 0)
        {
            return s_pBoardSound;
        }

        BoardChimeStop ();

        // Another output, chosen in the boot menu. The device in hand can only
        // go once it has stopped: Cancel() lets the DMA finish its buffer, and
        // only then is the state one the destructor accepts. Called between
        // two starts of a Macintosh, never while one is running, so nothing
        // is writing to it meanwhile.
        if (s_pBoardSound != 0)
        {
            s_pBoardSound->Cancel ();
            for (unsigned i = 0; i < 100 && s_pBoardSound->IsActive (); i++)
            {
                CTimer::Get ()->MsDelay (10);
            }
            if (s_pBoardSound->IsActive ())
            {
                CLogger::Get ()->Write (FROM, LogWarning,
                                        "Sound device %s would not stop; keeping it "
                                        "until the board restarts", s_BoardSoundWhere);
                return 0;
            }
            delete s_pBoardSound;
            s_pBoardSound = 0;
        }
        CLogger::Get ()->Write (FROM, LogNotice, "Sound output %s replaces %s",
                                pWhere, s_BoardSoundWhere);
    }
    s_bBoardSoundTried = true;
    memset (s_BoardSoundWhere, 0, sizeof s_BoardSoundWhere);
    strncpy (s_BoardSoundWhere, pWhere, sizeof s_BoardSoundWhere - 1);

    CSoundBaseDevice *pSound = 0;
    if (strcmp (pWhere, "hdmi") == 0)
    {
        pSound = new CHDMISoundBaseDevice (CInterruptSystem::Get (), SOUND_SAMPLE_RATE);
    }
    else if (strcmp (pWhere, "usb") == 0)
    {
#if RASPPI >= 4
        pSound = new CUSBSoundBaseDevice (SOUND_SAMPLE_RATE);
#else
        // Circle builds USB audio for a Pi 4 and a Pi 5 only
        // (lib/sound/Makefile:37).
        CLogger::Get ()->Write (FROM, LogWarning, "USB sound needs a Pi 4 or 5");
        return 0;
#endif
    }
    else
    {
        pSound = new CPWMSoundBaseDevice (CInterruptSystem::Get (), SOUND_SAMPLE_RATE);
    }
    if (pSound == 0)
    {
        return 0;
    }

    pSound->SetWriteFormat (SoundFormatSigned16, 2);
    if (!pSound->AllocateQueue (nQueueMsecs))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Cannot allocate the sound queue");
        delete pSound;          // never started, so there is nothing to wait for
        return 0;
    }

    // Started once and left running: a queue-mode device plays silence while
    // its queue is empty, so an engine that stops feeding it is all a close is.
    if (!pSound->Start () || !pSound->IsActive ())
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Sound device %s would not start", pWhere);
        return 0;               // kept, not deleted: see hal_circle.h
    }

    s_pBoardSound = pSound;
    CLogger::Get ()->Write (FROM, LogNotice, "Sound device %s claimed",
                            pWhere);
    return s_pBoardSound;
}

/*
 *  The startup chime — see hal_circle.h
 */

#include <circle/synchronize.h>

#include "sysdeps.h"
#include "prefs.h"
#include "rom_chime.h"

static const unsigned SOUND_QUEUE_MSECS = 100;     // audio_circle.cpp's, which claims too
static const u32      CHIME_MAX_FRAMES  = 262144;  // six seconds at 44.1 kHz

static s16                *s_pChime;                // 44.1 kHz stereo, rendered once found
static u32                s_nChimeFrames;
static volatile u32       s_nChimeOut;             // frames written
static volatile bool      s_bChimeStop;
static volatile bool      s_bChimePlaying;         // until the last frame is queued
static TKernelTimerHandle s_hChimeTimer;

// The resource map is a chain of records spread over the whole image, and a
// seek per field was a card read per field: 1.6 s to find the chime, measured.
// The image is therefore read in one go — a single sequential read the card
// serves at its full rate — and searched in memory. Both buffers are taken once
// and kept: a restart renders into the same ones.
static const u32 CHIME_ROM_MAX   = 0x400000;       // the largest ROM image
static const u32 CHIME_ROM_MIN   = 0x80000;        // the smallest that has a chime

static u8 *s_pChimeRom;

// A kernel timer, so at interrupt level on core 0: no allocation, no log, no
// card. Whatever room the queue has, then again in two ticks until the last
// frame.
static void ChimeTick (TKernelTimerHandle hTimer, void *pParam, void *pContext)
{
    s_hChimeTimer = 0;
    if (s_bChimeStop || s_pBoardSound == 0 || s_nChimeOut >= s_nChimeFrames)
    {
        s_bChimePlaying = false;
        return;
    }

    unsigned nRoom = s_pBoardSound->GetQueueSizeFrames () - s_pBoardSound->GetQueueFramesAvail ();
    if (nRoom > s_nChimeFrames - s_nChimeOut)
    {
        nRoom = s_nChimeFrames - s_nChimeOut;
    }
    if (nRoom != 0)
    {
        const int nWritten = s_pBoardSound->Write (s_pChime + s_nChimeOut * 2, nRoom * 4);
        s_nChimeOut += nWritten > 0 ? (u32) nWritten / 4 : 0;
    }

    s_hChimeTimer = CTimer::Get ()->StartKernelTimer (2, ChimeTick, 0, 0);
}

// Which ROM the startup volume's Macintosh takes: the first disk, and the
// engine the card remembers for it. A volume with no line is a 68k one.
static const char *ChimeRomPath (void)
{
    const char *pDisk = PrefsFindString ("disk", 0);
    bool bPowerPC = false;
    if (pDisk != 0)
    {
        if (*pDisk == '*')
        {
            pDisk++;
        }
        const size_t nDisk = strlen (pDisk);
        for (int i = 0; ; i++)
        {
            const char *pLine = PrefsFindString ("engine", i);
            if (pLine == 0)
            {
                break;
            }
            if (strncmp (pLine, pDisk, nDisk) == 0 && pLine[nDisk] == ' ')
            {
                bPowerPC = pLine[nDisk + 1] == 'p';
                break;
            }
        }
    }
    const char *pROM = bPowerPC ? PrefsFindString ("romppc") : 0;
    return pROM != 0 && *pROM != '\0' ? pROM : PrefsFindString ("rom");
}

// Loads one ROM file, finds its chime and renders it into s_pChime. Says why
// not when bExplain.
static bool ChimeLoad (const char *pROM, bool bExplain)
{
    char Path[256];
    snprintf (Path, sizeof Path, "SD:%s%s", *pROM == '/' ? "" : "/", pROM);

    FIL File;
    if (f_open (&File, Path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
    {
        if (bExplain)
        {
            CLogger::Get ()->Write (FROM, LogNotice, "Chime: cannot open %s", pROM);
        }
        return false;
    }

    const u32 nSize = (u32) f_size (&File);
    TRomChimeResult Result = RomChimeNone;
    TRomChime Chime;
    if (nSize >= CHIME_ROM_MIN && nSize <= CHIME_ROM_MAX)
    {
        if (s_pChimeRom == 0)
        {
            s_pChimeRom = new u8[CHIME_ROM_MAX];
        }
        if (s_pChime == 0)
        {
            s_pChime = new s16[CHIME_MAX_FRAMES * 2];
        }
        UINT nRead;
        if (   s_pChimeRom != 0 && s_pChime != 0
            && f_read (&File, s_pChimeRom, nSize, &nRead) == FR_OK && nRead == nSize)
        {
            Result = RomChimeFind (s_pChimeRom, nSize, &Chime);
        }
    }
    f_close (&File);

    if (Result == RomChimeFound)
    {
        s_nChimeFrames = RomChimeRender (s_pChimeRom, nSize, &Chime, s_pChime, CHIME_MAX_FRAMES);
        {
            static const char *KINDS[] = { "recorded, 'beep' 0", "recorded, 'snd '",
                                           "Apple Sound Chip table" };
            CLogger::Get ()->Write (FROM, LogNotice, "Chime: %s keeps one (%s)", pROM,
                                    KINDS[Chime.Kind]);
        }
        return s_nChimeFrames != 0;
    }
    if (bExplain)
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Chime: %s %s", pROM,
                                Result == RomChimeMalformed
                                    ? "has a chime this reader does not understand"
                                    : "keeps none this reader knows");
    }
    return false;
}

// The ROM the startup volume asks for first; then, when that one keeps no
// chime, the first on the card that does — the preferences' other ROM, then any
// .rom file at the root — so an unknown ROM still leaves the card a chime.
static bool ChimeFind (const char **ppFrom, char *pFound, size_t nFound)
{
    const char *pWanted = ChimeRomPath ();
    if (pWanted != 0 && *pWanted != '\0' && ChimeLoad (pWanted, true))
    {
        *ppFrom = pWanted;
        return true;
    }

    const char *Others[2] = { PrefsFindString ("romppc"), PrefsFindString ("rom") };
    for (unsigned i = 0; i < 2; i++)
    {
        if (   Others[i] != 0 && *Others[i] != '\0'
            && (pWanted == 0 || strcmp (Others[i], pWanted) != 0)
            && ChimeLoad (Others[i], false))
        {
            *ppFrom = Others[i];
            return true;
        }
    }

    FATFS_DIR Dir;
    FILINFO Info;
    if (f_opendir (&Dir, "SD:/") != FR_OK)
    {
        return false;
    }
    bool bFound = false;
    while (!bFound && f_readdir (&Dir, &Info) == FR_OK && Info.fname[0] != '\0')
    {
        const size_t nName = strlen (Info.fname);
        if (   (Info.fattrib & AM_DIR) != 0 || Info.fsize < CHIME_ROM_MIN || Info.fsize > CHIME_ROM_MAX || nName < 5
            || strcasecmp (Info.fname + nName - 4, ".rom") != 0)
        {
            continue;
        }
        snprintf (pFound, nFound, "/%s", Info.fname);
        bFound = (pWanted == 0 || strcmp (pFound, pWanted) != 0) && ChimeLoad (pFound, false);
    }
    f_closedir (&Dir);
    if (bFound)
    {
        *ppFrom = pFound;
    }
    return bFound;
}

void BoardChimePlay (void)
{
    // audio_circle.cpp's reading of the same two preferences.
    const char *pWhere = PrefsFindString ("soundoutput");
    if (pWhere == 0)
    {
        pWhere = PrefsFindBool ("nosound") ? "off" : "jack";
    }
    if (   PrefsFindBool ("nosound") || strcmp (pWhere, "off") == 0
        || strcmp (pWhere, "discard") == 0)
    {
        return;
    }

    // Searched again only when the ROM asked for has changed: a restart that
    // starts the same Macintosh reuses what the last search found, or did not.
    static bool s_bSearched, s_bHave;
    static char Wanted[256], Found[256];
    const char *pWanted = ChimeRomPath ();
    if (pWanted == 0)
    {
        pWanted = "";
    }
    if (!s_bSearched || strcmp (pWanted, Wanted) != 0)
    {
        s_bSearched = true;
        strncpy (Wanted, pWanted, sizeof Wanted - 1);
        const unsigned nStart = CTimer::GetClockTicks ();
        const char *pFrom = 0;
        static char Scanned[256];
        s_bHave = ChimeFind (&pFrom, Scanned, sizeof Scanned);
        if (!s_bHave)
        {
            CLogger::Get ()->Write (FROM, LogNotice, "Chime: no ROM on the card keeps one");
            return;
        }
        strncpy (Found, pFrom, sizeof Found - 1);
        CLogger::Get ()->Write (FROM, LogNotice, "Chime: %u ms from %s, found in %u ms",
                                (unsigned) (s_nChimeFrames * 1000 / ROM_CHIME_OUTPUT_RATE), Found,
                                (CTimer::GetClockTicks () - nStart) / 1000);
    }
    if (!s_bHave)
    {
        return;
    }

    BoardChimeStop ();
    if (BoardSoundClaim (pWhere, SOUND_QUEUE_MSECS) == 0)
    {
        return;
    }
    s_nChimeOut    = 0;
    s_bChimeStop   = false;
    s_bChimePlaying = true;
    s_hChimeTimer  = CTimer::Get ()->StartKernelTimer (1, ChimeTick, 0, 0);
    CLogger::Get ()->Write (FROM, LogNotice, "Chime: playing on %s, %u ms after the board started",
                            pWhere, CTimer::GetClockTicks () / 1000);
}

void BoardChimeStop (void)
{
    EnterCritical (IRQ_LEVEL);
    s_bChimeStop = true;
    s_bChimePlaying = false;
    if (s_hChimeTimer != 0)
    {
        CTimer::Get ()->CancelKernelTimer (s_hChimeTimer);
        s_hChimeTimer = 0;
    }
    LeaveCritical ();
}

void BoardChimeFinish (void)
{
    for (unsigned i = 0; i < 400 && s_bChimePlaying; i++)
    {
        CTimer::Get ()->MsDelay (10);
    }
    BoardChimeStop ();
}
