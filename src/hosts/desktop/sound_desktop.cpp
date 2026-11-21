//
// sound_desktop.cpp — the board's sound device and its chime, on a desktop.
//
// audio_okapia.cpp, the Pi's pump, is compiled unchanged: it pulls the
// Macintosh's samples on the tick and writes them into the device's queue, as
// into Circle's PWM, HDMI or USB device. Here the queue is an SDL audio stream
// on the host's default output, 44.1 kHz 16-bit stereo, and every output the
// preferences name ("jack", "hdmi", "usb") is that one; "off" stays silent,
// decided by audio_okapia.cpp as on the board.
//
// The chime is board_sound_circle.cpp's idea, read from the ROM the startup
// volume will use (rom_chime.cpp), rendered once and queued whole: the stream
// plays it while the boot menu's window is grey.
//
// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
//

#include "okapia_circle.h"
#include "hal_circle.h"
#include "okapia_card.h"
#include "okapia_host.h"
#include "rom_chime.h"

#include <circle/sound/soundbasedevice.h>

#include "sysdeps.h"
#include "prefs.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FROM "okapia-sound"

static const unsigned RATE = 44100;
static const unsigned FRAME_BYTES = 4;          // 16-bit stereo

static SDL_AudioStream  *s_pStream;
static CSoundBaseDevice  s_Device;
static unsigned          s_nQueueFrames = RATE / 10;

// The stream, once for the life of the process, as the board's device is.
static bool OpenStream (void)
{
    if (s_pStream != 0)
    {
        return true;
    }
    if (!SDL_WasInit (SDL_INIT_AUDIO) && !SDL_InitSubSystem (SDL_INIT_AUDIO))
    {
        HostLog (FROM, HostLogWarning, "No audio: %s", SDL_GetError ());
        return false;
    }
    const SDL_AudioSpec Spec = { SDL_AUDIO_S16, 2, (int) RATE };
    s_pStream = SDL_OpenAudioDeviceStream (SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &Spec, 0, 0);
    if (s_pStream == 0)
    {
        HostLog (FROM, HostLogWarning, "No audio output: %s", SDL_GetError ());
        return false;
    }
    SDL_ResumeAudioStreamDevice (s_pStream);
    HostLog (FROM, HostLogNotice, "Sound: the host's default output (%s), %u Hz",
             SDL_GetCurrentAudioDriver (), RATE);
    return true;
}

CSoundBaseDevice *BoardSoundClaim (const char *pWhere, unsigned nQueueMsecs)
{
    (void) pWhere;
    if (!OpenStream ())
    {
        return 0;
    }
    s_nQueueFrames = RATE * nQueueMsecs / 1000;
    return &s_Device;
}

int CSoundBaseDevice::Write (const void *pBuffer, size_t nCount)
{
    if (s_pStream == 0 || !SDL_PutAudioStreamData (s_pStream, pBuffer, (int) nCount))
    {
        return -1;
    }
    return (int) nCount;
}

unsigned CSoundBaseDevice::GetQueueSizeFrames (void)
{
    return s_nQueueFrames;
}

unsigned CSoundBaseDevice::GetQueueFramesAvail (void)
{
    const int nQueued = s_pStream != 0 ? SDL_GetAudioStreamQueued (s_pStream) : 0;
    const unsigned nFrames = nQueued > 0 ? (unsigned) nQueued / FRAME_BYTES : 0;
    return nFrames >= s_nQueueFrames ? 0 : s_nQueueFrames - nFrames;
}

bool CSoundBaseDevice::IsActive (void)
{
    return s_pStream != 0;
}

/*
 *  The chime
 */
static const unsigned ROM_MIN = 512 * 1024, ROM_MAX = 4 * 1024 * 1024;

// The ROM the startup volume asks for: its engine line says PowerPC, romppc;
// otherwise rom (board_sound_circle.cpp, ChimeRomPath).
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

static bool ChimeQueue (const char *pROM)
{
    if (pROM == 0 || *pROM == '\0')
    {
        return false;
    }
    FILE *pFile = fopen (CardPath (pROM), "rb");
    if (pFile == 0)
    {
        return false;
    }
    u8 *pRom = (u8 *) malloc (ROM_MAX);
    const size_t nSize = pRom != 0 ? fread (pRom, 1, ROM_MAX, pFile) : 0;
    fclose (pFile);

    bool bQueued = false;
    TRomChime Chime;
    if (nSize >= ROM_MIN && nSize <= ROM_MAX
        && RomChimeFind (pRom, (u32) nSize, &Chime) == RomChimeFound)
    {
        static const unsigned MAX_FRAMES = RATE * 6;
        s16 *pFrames = (s16 *) malloc (MAX_FRAMES * FRAME_BYTES);
        const unsigned nFrames = pFrames != 0
            ? RomChimeRender (pRom, (u32) nSize, &Chime, pFrames, MAX_FRAMES) : 0;
        if (nFrames != 0 && SDL_PutAudioStreamData (s_pStream, pFrames, (int) (nFrames * FRAME_BYTES)))
        {
            HostLog (FROM, HostLogNotice, "Chime: from %s, %u ms", pROM, nFrames * 1000 / RATE);
            bQueued = true;
        }
        free (pFrames);
    }
    free (pRom);
    return bQueued;
}

void BoardChimePlay (void)
{
    const char *pWhere = PrefsFindString ("soundoutput");
    if (PrefsFindBool ("nosound") || (pWhere != 0 && strcmp (pWhere, "off") == 0)
        || (pWhere != 0 && strcmp (pWhere, "discard") == 0))
    {
        return;                         // a Macintosh set silent chimes silently
    }
    if (!OpenStream ())
    {
        return;
    }
    const char *pWanted = ChimeRomPath ();
    if (!ChimeQueue (pWanted)
        && !ChimeQueue (PrefsFindString ("romppc"))
        && !ChimeQueue (PrefsFindString ("rom")))
    {
        HostLog (FROM, HostLogNotice, "Chime: no ROM on the card keeps one this reader knows");
    }
}

// Before the Macintosh feeds the same stream: the chime is let finish, up to a
// few seconds, as on the board.
void BoardChimeFinish (void)
{
    for (unsigned i = 0; i < 300 && s_pStream != 0 && SDL_GetAudioStreamQueued (s_pStream) > 0; i++)
    {
        SDL_Delay (10);
    }
}

void BoardChimeStop (void)
{
    if (s_pStream != 0)
    {
        SDL_ClearAudioStream (s_pStream);
    }
}
