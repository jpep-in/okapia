// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// The board's sound device, as audio_okapia.cpp feeds it: 44.1 kHz 16-bit
// stereo frames written into a queue. On a desktop the queue is an SDL audio
// stream on the default output (sound_desktop.cpp).
#ifndef _circle_sound_soundbasedevice_h
#define _circle_sound_soundbasedevice_h
#include <stddef.h>
class CSoundBaseDevice
{
public:
    int Write (const void *pBuffer, size_t nCount);     // bytes; answers bytes taken
    unsigned GetQueueSizeFrames (void);
    unsigned GetQueueFramesAvail (void);
    bool IsActive (void);
};
#endif
