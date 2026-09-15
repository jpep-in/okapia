/*
 * rom_chime.h — the startup chime, found in the Macintosh's own ROM.
 *
 * A Macintosh keeps its chime in one of three ways, and which one follows its
 * history (docs/notes/research/startup-chime.md): synthesised while it was a plain
 * chord, recorded once it became a synthesiser's sound.
 *
 *  - Macintosh II and IIci: a 32-byte table the ROM hands the Apple Sound Chip
 *    — four voices, their frequencies and three step counts. Nothing is
 *    recorded; the sound is rebuilt here by the chip's own arithmetic.
 *  - Quadra: a sampled 'snd ' — 8-bit, 22 254.5 Hz — stored as plain data,
 *    outside the ROM's resource map.
 *  - Power Macintosh of the 9500 family: the resource 'beep' 0, sixteen-bit
 *    stereo samples behind the DBDMA command list that played them.
 *
 * All three were checked against recordings of the real machines (§4.10).
 * Nothing of Apple's is in this repository: the chime is taken from the ROM the
 * card was given, when the board starts.
 *
 * Pure: no Circle and no card, so tests/host checks it against ROMs it builds.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_ROM_CHIME_H
#define OKAPIA_ROM_CHIME_H

#include <circle/types.h>

enum TRomChimeKind
{
    RomChimeBeep,           // 'beep' 0: big-endian 16-bit stereo, 22 050 Hz
    RomChimeSampled,        // a 'snd ' header: unsigned 8-bit mono, its own rate
    RomChimeSynthesised     // a 32-byte table for the Apple Sound Chip
};

struct TRomChime
{
    TRomChimeKind Kind;
    u32 nOffset;            // samples, or the table
    u32 nFrames;            // samples only
    u32 nRate;              // samples only, 16.16 fixed point
};

// Why a ROM gave no chime, for the log.
enum TRomChimeResult
{
    RomChimeFound,
    RomChimeNone,           // none of the three is in this image
    RomChimeMalformed       // a 'beep' 0 this reader does not understand
};

// Tried in the order the history suggests: a recording first, in the resource
// map and then as plain data, and the synthesiser's table last — a Quadra ROM
// still carries the Macintosh II's table from the code they share.
TRomChimeResult RomChimeFind (const u8 *pRom, u32 nRomBytes, TRomChime *pOut);

// The chime as 16-bit stereo at 44 100 Hz, at half scale: a Macintosh chimed at
// the volume its parameter RAM set, and nothing has read that at this point.
// Answers the frames written, at most nMaxFrames.
unsigned RomChimeRender (const u8 *pRom, u32 nRomBytes, const TRomChime *pChime,
                         s16 *pStereo, unsigned nMaxFrames);

static const unsigned ROM_CHIME_OUTPUT_RATE = 44100;

#endif
