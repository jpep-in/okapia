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
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "sysdeps.h"
#include "okapia_circle.h"
#include <circle/sound/soundbasedevice.h>
#include <string.h>

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "user_strings.h"
#include "audio.h"
#include "audio_defs.h"
#include "emul_op.h"
#include "hal_circle.h"

#define FROM "okapia-audio"

// 44100 Hz, 16-bit, stereo — the one format offered, as the dummy does.
static const unsigned SAMPLE_RATE  = 44100;
static const unsigned FRAMES_BLOCK = 1024;      // per Mac block
static const unsigned QUEUE_MSECS  = 100;
static const unsigned QUEUE_FRAMES = SAMPLE_RATE * QUEUE_MSECS / 1000;
// What 44.1 kHz consumes in one Mac tick of 16625 us.
static const unsigned FRAMES_PER_TICK = SAMPLE_RATE * 16625 / 1000000;
// Half a second with no room for a block while the Mac is playing: the device
// has stopped taking samples, and from here they are pulled and dropped.
static const unsigned STALL_TICKS = 30;

static CSoundBaseDevice *s_pSound;
static bool     s_bOpen;
static unsigned s_nBlocks;
static unsigned s_nUnderruns;

// Every step of the pull, counted, because a sound that never finishes leaves a
// Macintosh frozen with its pointer still moving and nothing else to say which
// step stopped: asked is INTFLAG_AUDIO raised, answered is AudioInterrupt()
// run, empty is an answer the mixer had no block for.
static unsigned s_nAsked;
static unsigned s_nAnswered;
static unsigned s_nEmpty;
static unsigned s_nStalls;

// "discard": no device at all. Blocks are pulled at the pace a device would
// take them and thrown away, which is the only way to exercise the Macintosh's
// half of the pull where no sound device starts — under QEMU, none does.
static bool     s_bDiscard;
static unsigned s_nDiscardQueued;       // what the pretend device still holds

// Frames the output still holds, and how many it can hold. Circle's
// GetQueueFramesAvail() is the first of these, *not* the room left
// (soundbasedevice.h:157, "frames available in the queue waiting to be sent");
// reading it as room is what kept a first sound from ever being pulled.
static unsigned FramesWaiting (void)
{
    return s_pSound != 0 ? s_pSound->GetQueueFramesAvail () : s_nDiscardQueued;
}

static unsigned FramesHeld (void)
{
    return s_pSound != 0 ? s_pSound->GetQueueSizeFrames () : QUEUE_FRAMES;
}

// One block, byte-swapped. Allocated once: nothing here may allocate later.
static int16 s_Block[FRAMES_BLOCK * 2];

// Left then right, 0x10000 for unity: the Mac's own volume (see below).
static uint32 s_nGain[2] = { 0x10000, 0x10000 };

void AudioInit (void)
{
    AudioStatus.sample_rate = SAMPLE_RATE << 16;
    AudioStatus.sample_size = 16;
    AudioStatus.channels    = 2;
    AudioStatus.mixer       = 0;
    AudioStatus.num_sources = 0;
    audio_component_flags   = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

    // Cleared first: the Macintosh goes round more than once, and a list that
    // grows by one entry per restart offers the Sound Manager the same rate
    // again and again.
    audio_sample_rates.clear ();
    audio_sample_sizes.clear ();
    audio_channel_counts.clear ();
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

    if (strcmp (pWhere, "discard") == 0)
    {
        s_bDiscard = true;
        s_bOpen    = true;
        audio_open = true;
        CLogger::Get ()->Write (FROM, LogNotice,
                                "Sound output: discard; pulled from the Macintosh "
                                "in real time and thrown away");
        return;
    }

    // A failure here must not stop the Mac from booting (docs/topics/sound.md): warn and
    // carry on silent. The board checks Start() and IsActive(), so a socket
    // it does not have leaves the Mac silent rather than waiting for a
    // completion that never comes. The device is the board's and outlives
    // this start (hal_circle.h): claiming one per start froze restarts.
    CLogger::Get ()->Write (FROM, LogNotice, "Sound output: %s", pWhere);
    BoardChimeFinish ();
    s_pSound = BoardSoundClaim (strcmp (pWhere, "hdmi") == 0 || strcmp (pWhere, "usb") == 0
                                ? pWhere : "jack", QUEUE_MSECS);
    if (s_pSound == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "No sound device; the Mac stays silent");
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
    // Not deleted, not even cancelled: the device belongs to the board and
    // plays silence once its queue has drained (hal_circle.h).
    s_pSound = 0;
    s_bOpen = false;
    s_bDiscard = false;
}

/*
 *  The Mac opened the sound component (audio.cpp:398), which it does long
 *  before any sound is played.
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
    s_nAnswered++;
    if (!AudioStatus.mixer)
    {
        WriteMacInt32 (audio_data + adatStreamInfo, 0);
        s_nEmpty++;
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
        s_nEmpty++;
        return;
    }

    uint32 nFrames = ReadMacInt32 (nInfo + scd_sampleCount);
    if (nFrames == 0)
    {
        s_nEmpty++;
        return;
    }
    if (nFrames > FRAMES_BLOCK)
    {
        nFrames = FRAMES_BLOCK;
    }

    if (s_bDiscard)
    {
        s_nDiscardQueued += nFrames;
        s_nBlocks++;
        return;
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

    if (s_nGain[0] != 0x10000 || s_nGain[1] != 0x10000)
    {
        for (uint32 i = 0; i < nFrames * 2; i++)
        {
            s_Block[i] = (int16) (((int32) s_Block[i] * (int32) s_nGain[i & 1]) >> 16);
        }
    }

    int nWritten = s_pSound->Write (s_Block, nFrames * 2 * sizeof (int16));
    if (nWritten < (int) (nFrames * 2 * sizeof (int16)))
    {
        s_nUnderruns++;
    }
    s_nBlocks++;

    static unsigned s_nStallsSaid;
    if (s_nStalls != s_nStallsSaid)
    {
        s_nStallsSaid = s_nStalls;
        CLogger::Get ()->Write (FROM, LogWarning,
                                "The sound device stopped taking samples; the "
                                "Macintosh's are dropped so that it does not wait "
                                "(%u times)", s_nStalls);
    }
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
    if (!s_bOpen || (s_pSound != 0 && !s_pSound->IsActive ()))
    {
        return;
    }

    // The pretend device plays in real time whether or not anything is queued.
    if (s_bDiscard)
    {
        s_nDiscardQueued -= s_nDiscardQueued < FRAMES_PER_TICK ? s_nDiscardQueued
                                                              : FRAMES_PER_TICK;
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

    // A block is asked for while the queue has room for one. Never waiting for
    // room for ever is the second half: a Macintosh playing a sound waits for
    // it to end, and a device that has stopped draining would hold it there
    // with its pointer still moving — which is how this was reported.
    static unsigned s_nNoRoom;
    if (FramesHeld () - FramesWaiting () >= FRAMES_BLOCK)
    {
        s_nNoRoom = 0;
    }
    else if (++s_nNoRoom < STALL_TICKS)
    {
        return;
    }
    else
    {
        s_nNoRoom = 0;
        s_nStalls++;
    }

    s_nAsked++;
    SetInterruptFlag (INTFLAG_AUDIO);
    TriggerInterrupt ();
}

void AudioReport (unsigned nSeconds)
{
    (void) nSeconds;
    static unsigned s_nSaidAsked = ~0u;
    if (!s_bOpen || (s_nAsked == s_nSaidAsked && AudioStatus.num_sources == 0))
    {
        return;
    }
    s_nSaidAsked = s_nAsked;
    CLogger::Get ()->Write (FROM, LogNotice,
                            "sound: %u sources, %u asked, %u answered (%u empty), "
                            "%u blocks, %u short writes, %u stalls, queue %u of %u frames",
                            AudioStatus.num_sources, s_nAsked, s_nAnswered, s_nEmpty,
                            s_nBlocks, s_nUnderruns, s_nStalls, FramesWaiting (),
                            FramesHeld ());
}

#if SOUND_SELFTEST && !defined (SHEEPSHAVER)
/*
 *  One system beep, the first time the Macintosh is idle
 *
 *  A sound nobody asked for, in a trace build only: it is the one way to make
 *  the Sound Manager play without anybody at the screen, and SysBeep waits for
 *  its sound to finish — so a pull that never happens shows as a line that
 *  never comes, rather than as a frozen Macintosh somebody has to report.
 */
void AudioSelfTest (void)
{
    static bool s_bDone;
    if (s_bDone || !s_bOpen)
    {
        return;
    }
    s_bDone = true;

    static const uint8 Proc[] =
    {
        0x3F, 0x3C, 0x00, 0x1E,                 // move.w  #30,-(sp)
        0xA9, 0xC8,                             // _SysBeep
        (uint8) (M68K_RTS >> 8), (uint8) (M68K_RTS & 0xFF)
    };
    M68kRegisters r;
    r.d[0] = sizeof Proc;
    Execute68kTrap (0xA71E, &r);                // NewPtrSysClear()
    const uint32 nProc = r.a[0];
    if (nProc == 0)
    {
        CLogger::Get ()->Write (FROM, LogWarning, "Sound selftest: no memory for the stub");
        return;
    }
    Host2Mac_memcpy (nProc, (void *) Proc, sizeof Proc);

    CLogger::Get ()->Write (FROM, LogNotice, "Sound selftest: SysBeep");
    Execute68k (nProc, &r);
    CLogger::Get ()->Write (FROM, LogNotice,
                            "Sound selftest: SysBeep returned after %u blocks, %u short writes",
                            s_nBlocks, s_nUnderruns);

    r.a[0] = nProc;
    Execute68kTrap (0xA01F, &r);                // DisposePtr()
}
#endif

/*
 *  Sampling parameters. Only one format is offered, so these just accept it.
 */

bool audio_set_sample_rate (int index)  { return index == 0; }
bool audio_set_sample_size (int index)  { return index == 0; }
bool audio_set_channels (int index)     { return index == 0; }

/*
 *  Mixer controls
 *
 *  Neither the jack nor HDMI has a mixer Circle can drive — CPWMSoundBaseDevice
 *  and CHDMISoundBaseDevice offer no controller at all — so the volume is
 *  applied to the samples, as upstream's SDL port does (audio_sdl.cpp:386).
 *  Remembering it without applying it left the Sound control panel's slider
 *  moving and the sound as loud as ever. Each word is left in its high half
 *  and right in its low, 0 to 0x100.
 */

static const uint32 MAC_MAX_VOLUME = 0x100;

static bool   s_bMainMute, s_bSpeakerMute;
static uint32 s_nMainVolume    = 0x01000100;
static uint32 s_nSpeakerVolume = 0x01000100;

static void UpdateGain (void)
{
    const bool bMute = s_bMainMute || s_bSpeakerMute;
    for (unsigned c = 0; c < 2; c++)
    {
        const unsigned nShift = c == 0 ? 16 : 0;
        uint32 nMain    = (s_nMainVolume    >> nShift) & 0xFFFF;
        uint32 nSpeaker = (s_nSpeakerVolume >> nShift) & 0xFFFF;
        if (nMain > MAC_MAX_VOLUME)    nMain = MAC_MAX_VOLUME;
        if (nSpeaker > MAC_MAX_VOLUME) nSpeaker = MAC_MAX_VOLUME;
        s_nGain[c] = bMute ? 0 : nMain * nSpeaker;      // 0 to 0x10000
    }
}

bool audio_get_main_mute (void)              { return s_bMainMute; }
uint32 audio_get_main_volume (void)          { return s_nMainVolume; }
bool audio_get_speaker_mute (void)           { return s_bSpeakerMute; }
uint32 audio_get_speaker_volume (void)       { return s_nSpeakerVolume; }
void audio_set_main_mute (bool mute)         { s_bMainMute = mute; UpdateGain (); }
void audio_set_main_volume (uint32 vol)      { s_nMainVolume = vol; UpdateGain (); }
void audio_set_speaker_mute (bool mute)      { s_bSpeakerMute = mute; UpdateGain (); }
void audio_set_speaker_volume (uint32 vol)   { s_nSpeakerVolume = vol; UpdateGain (); }
