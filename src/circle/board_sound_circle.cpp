/*
 * board_sound_circle.cpp — the board's sound, its chime, and its last act.
 *
 * These three were part of hal_circle.cpp and had to leave it. The board is
 * brought up by two kernels — the Macintosh one and the theme specimen, which
 * links Circle and the firmware's drawing code and nothing else — and that is
 * the whole value of hal_circle.cpp: the specimen proves the firmware runs on a
 * real board rather than on a copy of itself. Sound reads the Macintosh's
 * preferences, the chime reads a Macintosh ROM, and the power-off claims the
 * firmware's frame buffer, so keeping them there made the specimen kernel
 * unbuildable — it has neither prefs.h nor okapia_output.cpp — and would have
 * meant giving it the emulator's include paths to render a page of type.
 *
 * So the board stays what hal_circle.h promises, and what needs a Macintosh
 * behind it lives here. This file is in SHARED_SRCS (src/kernel/Makefile) and
 * not in the specimen's object list; nothing in the specimen calls any of it.
 *
 * The three sit together rather than in three files because they are one piece
 * of state: BoardPowerOff() silences the device BoardSoundClaim() owns, and the
 * chime feeds that same device before either Macintosh has started. Splitting
 * them would mean publishing s_pBoardSound, which is the thing hal_circle.h
 * spends a paragraph on not doing.
 *
 * Unlike the rest of the shared half, this names the emulator's headers. That
 * costs nothing across the two engines — prefs.h is one file, symbolically
 * linked between the two trees — and the shared half is compiled once anyway.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal_circle.h"

#include <circle/bcmpropertytags.h>
#include <circle/logger.h>
#include <circle/timer.h>

#include <stdio.h>
#include <string.h>

// The same name hal_circle.cpp logs under: a reader is looking at one board,
// and where a line was compiled is not what they are asking.
#define FROM "okapia-board"

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

/*
 *  Before halt() — see hal_circle.h
 */

// Not in Circle's list. The firmware's mailbox property interface, "Blank
// screen": one u32, bit 0 set to blank.
static const u32 PROPTAG_BLANK_SCREEN = 0x00040002;

void BoardPowerOff (void)
{
    if (s_pBoardSound != 0)
    {
        s_pBoardSound->Cancel ();
        // The DMA stops at the end of its current buffer; halt() disables the
        // interrupts that would let it, so give it the few buffers it needs.
        for (unsigned i = 0; i < 20 && s_pBoardSound->IsActive (); i++)
        {
            CTimer::Get ()->MsDelay (10);
        }
    }

    CBcmFrameBuffer *pOutput = FwOutputClaim ();
    if (pOutput != 0)
    {
        // One call over the whole buffer, as the PowerPC engine's stop already
        // did: it starts aligned and is a whole number of 16-byte rows, which
        // is what keeps a vectorised store off an alignment fault (docs/topics/display.md).
        memset ((void *) (uintptr) pOutput->GetBuffer (), 0,
                (size_t) pOutput->GetPitch () * pOutput->GetHeight ());
    }

    CBcmPropertyTags Tags;
    TPropertyTagSimple Blank;
    Blank.nValue = 1;
    const bool bBlanked = Tags.GetTag (PROPTAG_BLANK_SCREEN, &Blank, sizeof Blank, 4);

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Stopped: the power can be switched off (display %s)",
                            bBlanked ? "blanked" : "cleared to black");
    BoardLogFlush ();
}
