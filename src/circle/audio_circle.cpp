/*
 * audio_circle.cpp — the Mac's sound output, on Circle.
 *
 * Basilisk's contract (audio_sdl.cpp is the model): raising INTFLAG_AUDIO makes
 * the 68k interpreter call AudioInterrupt() (emul_op.cpp:504), which asks the
 * Mac's mixer for the next block by running 68k code. That is why the pull has
 * to start from the emulation side and not from a DMA interrupt: Execute68k()
 * belongs to the 68k context.
 *
 * So the flow is: the 60 Hz tick notices the queue is running low, raises the
 * flag, and the block that AudioInterrupt() produces is byte-swapped into
 * Circle's own queue, which its DMA drains. The Mac emits big-endian samples
 * (SDL asks for AUDIO_S16MSB); Circle's formats are all little-endian.
 *
 * Copyright (C) 1997-2008 Christian Bauer et al.
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/sound/pwmsoundbasedevice.h>
#if RASPPI >= 4
#include <circle/sound/usbsoundbasedevice.h>
#endif
#include <circle/interrupt.h>
#include <string.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"

#define FROM "okapia-audio"

// 44100 Hz, 16-bit, stereo — the one format offered, as the dummy does.
static const unsigned SAMPLE_RATE  = 44100;
static const unsigned FRAMES_BLOCK = 1024;      // per Mac block
static const unsigned QUEUE_MSECS  = 100;

static CSoundBaseDevice *s_pSound;
static bool     s_bOpen;
static unsigned s_nBlocks;
static unsigned s_nUnderruns;

// One block, byte-swapped. Allocated once: nothing here may allocate later.
static int16 s_Block[FRAMES_BLOCK * 2];

void AudioInit (void)
{
    AudioStatus.sample_rate = SAMPLE_RATE << 16;
    AudioStatus.sample_size = 16;
    AudioStatus.channels    = 2;
    AudioStatus.mixer       = 0;
    AudioStatus.num_sources = 0;
    audio_component_flags   = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

    audio_sample_rates.push_back (SAMPLE_RATE << 16);
    audio_sample_sizes.push_back (16);
    audio_channel_counts.push_back (2);

    audio_open             = false;
    audio_frames_per_block = FRAMES_BLOCK;

    // Where it comes out, and not merely whether. The jack used to be hard-coded
    // here, which a Pi 5 does not have and which is the wrong socket on a
    // television — so the firmware offers four states and this reads the one it
    // wrote. An unknown value is silence, deliberately: a device claimed and
    // not working is what froze the guest once already, and cost a card.
    const char *pWhere = PrefsFindString ("soundoutput");
    if (pWhere == 0)
    {
        // A card written before this setting existed still says what it wants.
        pWhere = PrefsFindBool ("nosound") ? "off" : "jack";
    }

    // nosound wins wherever the two disagree, which they only can on a card
    // edited by hand: the quieter reading of a contradiction is the safe one.
    if (strcmp (pWhere, "off") == 0 || PrefsFindBool ("nosound"))
    {
        CLogger::Get ()->Write (FROM, LogNotice, "Sound disabled by preference");
        return;
    }

    // A failure here must not stop the Mac from booting (AGENTS.md): warn and
    // carry on silent. Every one of these is checked with Start() and
    // IsActive() below, so a socket the board does not have leaves the Mac
    // silent rather than waiting for a completion that never comes.
    if (strcmp (pWhere, "hdmi") == 0)
    {
        s_pSound = new CHDMISoundBaseDevice (CInterruptSystem::Get (), SAMPLE_RATE);
    }
    else if (strcmp (pWhere, "usb") == 0)
    {
#if RASPPI >= 4
        s_pSound = new CUSBSoundBaseDevice (SAMPLE_RATE);
#else
        // Circle builds USB audio for a Pi 4 and a Pi 5 only
        // (lib/sound/Makefile:37). Say so and stay silent rather than reach for
        // a socket this board's stack cannot drive.
        CLogger::Get ()->Write (FROM, LogWarning,
                                "USB sound needs a Pi 4 or 5; staying silent");
        return;
#endif
    }
    else
    {
        s_pSound = new CPWMSoundBaseDevice (CInterruptSystem::Get (), SAMPLE_RATE);
    }
    if (s_pSound == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No sound device; staying silent");
        return;
    }
    CLogger::Get ()->Write (FROM, LogNotice, "Sound output: %s", pWhere);

    s_pSound->SetWriteFormat (SoundFormatSigned16, 2);
    if (!s_pSound->AllocateQueue (QUEUE_MSECS))
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Cannot allocate the sound queue");
        delete s_pSound;
        s_pSound = 0;
        return;
    }

    // Start now rather than when the Mac adds its first source. A queue-mode
    // device plays silence while the queue is empty, and starting here means a
    // refusal is discovered before anything has been promised to the Mac.
    if (!s_pSound->Start () || !s_pSound->IsActive ())
    {
        CLogger::Get ()->Write (FROM, LogWarning,
                                "Sound device would not start; the Mac stays silent");
        delete s_pSound;
        s_pSound = 0;
        return;                 // audio_open stays false, exactly like the dummy
    }

    s_bOpen    = true;
    audio_open = true;

    CLogger::Get ()->Write (FROM, LogNotice,
                            "Sound ready: %u Hz, 16-bit stereo, %u ms queue, %u frames per block",
                            SAMPLE_RATE, QUEUE_MSECS, FRAMES_BLOCK);
}

void AudioExit (void)
{
    if (s_pSound != 0)
    {
        s_pSound->Cancel ();
        delete s_pSound;
        s_pSound = 0;
    }
    s_bOpen = false;
}

/*
 *  The Mac added its first source, so sound is actually wanted now.
 */

void audio_enter_stream (void)
{
    // The device is already running and plays silence when the queue is empty.
    // Starting it here instead would mean discovering a refusal from the 68k
    // context, with the Mac already committed to a sound it will never hear.
}

void audio_exit_stream (void)
{
}

/*
 *  Called from the 68k context once INTFLAG_AUDIO has been raised. The mixer
 *  hands back a pointer to its block; we swap it into the queue.
 */

void AudioInterrupt (void)
{
    if (!AudioStatus.mixer)
    {
        WriteMacInt32 (audio_data + adatStreamInfo, 0);
        return;
    }

    M68kRegisters r;
    r.a[0] = audio_data + adatStreamInfo;
    r.a[1] = AudioStatus.mixer;
    Execute68k (audio_data + adatGetSourceData, &r);

    if (!s_bOpen)
    {
        return;
    }

    uint32 nInfo = ReadMacInt32 (audio_data + adatStreamInfo);
    if (nInfo == 0)
    {
        return;
    }

    uint32 nFrames = ReadMacInt32 (nInfo + scd_sampleCount);
    if (nFrames == 0)
    {
        return;
    }
    if (nFrames > FRAMES_BLOCK)
    {
        nFrames = FRAMES_BLOCK;
    }

    const uint8 *pSrc = Mac2HostAddr (ReadMacInt32 (nInfo + scd_buffer));

    // A mono 8-bit source is doubled into both channels, as upstream does.
    const bool bDouble =    ReadMacInt16 (nInfo + scd_numChannels) == 1
                         && ReadMacInt16 (nInfo + scd_sampleSize)  == 8;
    if (bDouble)
    {
        for (uint32 i = 0; i < nFrames; i++)
        {
            int16 v = (int16) ((pSrc[i] - 128) << 8);
            s_Block[i * 2]     = v;
            s_Block[i * 2 + 1] = v;
        }
    }
    else
    {
        // Big-endian pairs to little-endian, two channels per frame.
        for (uint32 i = 0; i < nFrames * 2; i++)
        {
            s_Block[i] = (int16) ((pSrc[i * 2] << 8) | pSrc[i * 2 + 1]);
        }
    }

    int nWritten = s_pSound->Write (s_Block, nFrames * 2 * sizeof (int16));
    if (nWritten < (int) (nFrames * 2 * sizeof (int16)))
    {
        s_nUnderruns++;
    }
    s_nBlocks++;
}

/*
 *  Called from the tick: ask the Mac for another block while the queue has room
 *  for one. Nothing here runs 68k code — it only raises the flag.
 *
 *  The grace period is why a sound does not lose its tail. The Mac's mixer
 *  reports num_sources == 0 the moment the last source has *finished feeding*,
 *  not when the last sample has been fetched, so stopping there leaves whatever
 *  it still holds unasked for — an alert cut a fraction short, which sounds
 *  like a click rather than like a missing block. Ten more asks at 60 Hz is a
 *  sixth of a second, and they cost nothing once the mixer has genuinely
 *  finished: it answers with no stream and AudioInterrupt() returns.
 */

static const unsigned AUDIO_GRACE_TICKS = 10;

void AudioPump (void)
{
    if (!s_bOpen || !s_pSound->IsActive ())
    {
        return;
    }

    static unsigned s_nGrace;
    if (AudioStatus.num_sources != 0)
    {
        s_nGrace = AUDIO_GRACE_TICKS;
    }
    else if (s_nGrace == 0)
    {
        return;
    }
    else
    {
        s_nGrace--;
    }

    if (s_pSound->GetQueueFramesAvail () >= FRAMES_BLOCK)
    {
        SetInterruptFlag (INTFLAG_AUDIO);
        TriggerInterrupt ();
    }
}

void AudioReport (unsigned nSeconds)
{
    if (s_bOpen && s_nBlocks > 0)
    {
        CLogger::Get ()->Write (FROM, LogNotice,
                                "%u blocks (%u frames/s), %u underruns",
                                s_nBlocks, s_nBlocks * FRAMES_BLOCK / (nSeconds ? nSeconds : 1),
                                s_nUnderruns);
    }
}

/*
 *  Sampling parameters. Only one format is offered, so these just accept it.
 */

bool audio_set_sample_rate (int index)  { return index == 0; }
bool audio_set_sample_size (int index)  { return index == 0; }
bool audio_set_channels (int index)     { return index == 0; }

/*
 *  Mixer controls. Circle's sound controller is not wired up yet, so the mute
 *  and volume the Mac asks for are remembered and reported back, not applied.
 */

static bool   s_bMainMute, s_bSpeakerMute;
static uint32 s_nMainVolume    = 0x01000100;
static uint32 s_nSpeakerVolume = 0x01000100;

bool audio_get_main_mute (void)              { return s_bMainMute; }
uint32 audio_get_main_volume (void)          { return s_nMainVolume; }
bool audio_get_speaker_mute (void)           { return s_bSpeakerMute; }
uint32 audio_get_speaker_volume (void)       { return s_nSpeakerVolume; }
void audio_set_main_mute (bool mute)         { s_bMainMute = mute; }
void audio_set_main_volume (uint32 vol)      { s_nMainVolume = vol; }
void audio_set_speaker_mute (bool mute)      { s_bSpeakerMute = mute; }
void audio_set_speaker_volume (uint32 vol)   { s_nSpeakerVolume = vol; }
