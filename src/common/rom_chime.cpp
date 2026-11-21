/*
 * rom_chime.cpp — see rom_chime.h, and docs/notes/research/startup-chime.md for how each
 * layout was established.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rom_chime.h"

static u32 BE32 (const u8 *p)
{
    return (u32) p[0] << 24 | (u32) p[1] << 16 | (u32) p[2] << 8 | p[3];
}

static u16 BE16 (const u8 *p)
{
    return (u16) (p[0] << 8 | p[1]);
}

/*
 *  'beep' 0, as a Power Macintosh of the 9500 family keeps it (gong.md §3)
 *
 *  The resource map is walked as SheepShaver walks it (rom_patches.cpp:226):
 *  the long at 0x1A points at the map, whose byte 5 is the size of each entry's
 *  header; every entry is a pointer to the next, then the header, then the data
 *  offset, the type and the ID. The resource itself is four longs — command
 *  list offset, channels, bytes of samples, their address — then 16-byte DBDMA
 *  commands, little-endian as the chip reads them, each an OUTPUT_MORE or
 *  OUTPUT_LAST over the next stretch of samples, ending in STOP.
 */

static const u32 TYPE_BEEP    = 0x62656570;    // 'beep'
static const u32 MAX_ENTRIES  = 4096;          // a loop in a corrupt map ends
static const u32 MAX_COMMANDS = 256;

static const unsigned DBDMA_OUTPUT_MORE = 0x0000;
static const unsigned DBDMA_OUTPUT_LAST = 0x1000;
static const unsigned DBDMA_STOP        = 0x7000;

// Samples at 22 050 Hz: not stated in the resource, and confirmed against a
// recording of the machine — same length, same partials (gong.md §3).
static const u32 BEEP_RATE = 22050u << 16;

static bool Inside (u32 nRomBytes, u32 nOffset, u32 nBytes)
{
    return nOffset <= nRomBytes && nBytes <= nRomBytes - nOffset;
}

static TRomChimeResult FindBeep (const u8 *pRom, u32 nRomBytes, TRomChime *pOut)
{
    if (!Inside (nRomBytes, 0x1A, 4))
    {
        return RomChimeNone;
    }
    const u32 nMap = BE32 (pRom + 0x1A);
    if (nMap == 0 || !Inside (nRomBytes, nMap, 8))
    {
        return RomChimeNone;
    }
    const u8 HeaderSize = pRom[nMap + 5];

    u32 nData = 0;
    u32 nEntry = nMap;
    for (u32 i = 0; i < MAX_ENTRIES && nData == 0; i++)
    {
        if (!Inside (nRomBytes, nEntry, 4))
        {
            return RomChimeNone;
        }
        nEntry = BE32 (pRom + nEntry);
        if (nEntry == 0 || !Inside (nRomBytes, nEntry + HeaderSize, 14))
        {
            return RomChimeNone;
        }
        nEntry += HeaderSize;
        if (BE32 (pRom + nEntry + 8) == TYPE_BEEP && BE16 (pRom + nEntry + 12) == 0)
        {
            nData = BE32 (pRom + nEntry + 4);
        }
    }
    if (nData == 0 || !Inside (nRomBytes, nData, 16))
    {
        return RomChimeNone;
    }

    const u32 nCommands = BE32 (pRom + nData);
    const u32 nChannels = BE32 (pRom + nData + 4);
    const u32 nBytes    = BE32 (pRom + nData + 8);
    const u32 nAddress  = BE32 (pRom + nData + 12);
    // The ROM is mapped at the top of the address space, so an address is its
    // offset in the image once the bits above the image's size are dropped.
    // That holds only for a size that is a power of two.
    if (   nChannels != 2 || nBytes == 0 || (nBytes % 4) != 0
        || (nRomBytes & (nRomBytes - 1)) != 0)
    {
        return RomChimeMalformed;
    }
    const u32 nSamples = nAddress & (nRomBytes - 1);

    // Every command must cover the next stretch, and together exactly the
    // stated count: anything else is a layout this reader has not seen.
    u32 nCovered = 0;
    u32 nAt = nData + nCommands;
    for (u32 i = 0; ; i++, nAt += 16)
    {
        if (i == MAX_COMMANDS || !Inside (nRomBytes, nAt, 16))
        {
            return RomChimeMalformed;
        }
        const u8 *c = pRom + nAt;
        const unsigned nLength  = c[0] | c[1] << 8;
        const unsigned nCommand = c[2] | c[3] << 8;
        const u32 nCmdAddress = c[4] | c[5] << 8 | (u32) c[6] << 16 | (u32) c[7] << 24;
        if (nCommand == DBDMA_STOP)
        {
            break;
        }
        if (   (nCommand != DBDMA_OUTPUT_MORE && nCommand != DBDMA_OUTPUT_LAST)
            || nCmdAddress != nAddress + nCovered)
        {
            return RomChimeMalformed;
        }
        nCovered += nLength;
    }
    if (nCovered != nBytes || !Inside (nRomBytes, nSamples, nBytes))
    {
        return RomChimeMalformed;
    }

    pOut->Kind    = RomChimeBeep;
    pOut->nOffset = nSamples;
    pOut->nFrames = nBytes / 4;
    pOut->nRate   = BEEP_RATE;
    return RomChimeFound;
}

/*
 *  A sampled 'snd ', stored as plain data (gong.md §4.9)
 *
 *  Format 1, one data format (sampledSynth), one bufferCmd whose parameter is
 *  the offset of the sound header, twenty bytes on; the header then gives the
 *  sample count, the rate in 16.16, the loop, the encoding and the base note.
 *  Only an uncompressed 8-bit header is accepted, and only one long enough to
 *  be a chime: the system alert, 'snd ' 1, is a tenth of a second.
 */

static const u32 SND_MIN_SAMPLES = 11025;

static bool FindSampled (const u8 *pRom, u32 nRomBytes, TRomChime *pOut)
{
    for (u32 o = 0; o + 42 <= nRomBytes; o += 2)
    {
        const u8 *p = pRom + o;
        if (   BE16 (p) != 1 || BE16 (p + 2) != 1 || BE16 (p + 4) != 5
            || BE16 (p + 10) != 1 || BE16 (p + 12) != 0x8051 || BE32 (p + 16) != 20)
        {
            continue;
        }
        const u8 *h = p + 20;
        const u32 nFrames = BE32 (h + 4);
        const u32 nRate   = BE32 (h + 8);
        if (   BE32 (h) != 0 || h[20] != 0
            || nFrames < SND_MIN_SAMPLES || nRate < (5000u << 16) || nRate > (48000u << 16)
            || !Inside (nRomBytes, o + 42, nFrames))
        {
            continue;
        }
        pOut->Kind    = RomChimeSampled;
        pOut->nOffset = o + 42;
        pOut->nFrames = nFrames;
        pOut->nRate   = nRate;
        return true;
    }
    return false;
}

/*
 *  The Apple Sound Chip's table (gong.md §4.3)
 *
 *  Volume, step, delay and total as big-endian words and longs, the voice
 *  count, then four frequencies. The routine counts all three with dbra, so
 *  none can exceed a word; the first table that fits is the boot chime, the
 *  error chimes following it.
 */

static bool FindSynthesised (const u8 *pRom, u32 nRomBytes, TRomChime *pOut)
{
    for (u32 o = 0; o + 32 <= nRomBytes; o += 2)
    {
        const u8 *p = pRom + o;
        const u32 nStep  = BE32 (p + 2);
        const u32 nDelay = BE32 (p + 6);
        const u32 nTotal = BE32 (p + 10);
        const u16 nCount = BE16 (p + 14);
        if (   nCount < 2 || nCount > 4 || nStep == 0 || nStep > 0xFF
            || nTotal < 1000 || nTotal > 0xFFFF || nDelay >= nTotal)
        {
            continue;
        }
        bool bOK = true;
        for (unsigned v = 0; v < 4 && bOK; v++)
        {
            const u32 nFreq = BE32 (p + 16 + v * 4);
            bOK = v < nCount ? nFreq > 0x4000 && nFreq < 0x80000 : nFreq == 0;
        }
        if (bOK)
        {
            pOut->Kind    = RomChimeSynthesised;
            pOut->nOffset = o;
            pOut->nFrames = 0;
            pOut->nRate   = 0;
            return true;
        }
    }
    return false;
}

TRomChimeResult RomChimeFind (const u8 *pRom, u32 nRomBytes, TRomChime *pOut)
{
    const TRomChimeResult Beep = FindBeep (pRom, nRomBytes, pOut);
    if (Beep != RomChimeNone)
    {
        return Beep;
    }
    if (FindSampled (pRom, nRomBytes, pOut) || FindSynthesised (pRom, nRomBytes, pOut))
    {
        return RomChimeFound;
    }
    return RomChimeNone;
}

/*
 *  Rendering
 */

static const s16 HALF_SCALE = 16384;

// Output frame j sits at input position j * rate / 44 100; linear between the
// two input frames around it.
static unsigned RenderSamples (const u8 *pRom, const TRomChime *pChime,
                               s16 *pStereo, unsigned nMaxFrames)
{
    const u64 nStep = ((u64) pChime->nRate << 16) / ROM_CHIME_OUTPUT_RATE;   // 32.32 per 16.16
    unsigned j = 0;
    for (u64 nPos = 0; j < nMaxFrames; nPos += nStep, j++)
    {
        const u32 i = (u32) (nPos >> 32);
        if (i >= pChime->nFrames)
        {
            break;
        }
        const u32 nFrac = (u32) (nPos & 0xFFFFFFFFu) >> 16;              // 0 to 0xFFFF
        const u32 i1 = i + 1 < pChime->nFrames ? i + 1 : i;
        for (unsigned c = 0; c < 2; c++)
        {
            s32 a, b;
            if (pChime->Kind == RomChimeBeep)
            {
                a = (s16) BE16 (pRom + pChime->nOffset + i * 4 + c * 2) / 2;
                b = (s16) BE16 (pRom + pChime->nOffset + i1 * 4 + c * 2) / 2;
            }
            else
            {
                a = ((s32) pRom[pChime->nOffset + i] - 128) * (HALF_SCALE / 128);
                b = ((s32) pRom[pChime->nOffset + i1] - 128) * (HALF_SCALE / 128);
            }
            pStereo[j * 2 + c] = (s16) (a + (((b - a) * (s32) nFrac) >> 16));
        }
    }
    return j;
}

// The chip's own arithmetic, as Snow and Mini vMac both model it (gong.md §4.4):
// 22 257 Hz, four 512-byte tables, a 17.15 phase per voice, the voices summed.
static const u32 ASC_RATE = 22257;

// One step of the routine, fitted against a recording of a Macintosh II
// (gong.md §4.5, §4.10): fourteen VIA accesses at 1.276 us and the step itself.
static const u32 STEP_NS = 22950;

// The first waveform, in 64-sample stairs. This is the one the original chip is
// given; its successors get another, which on this chime does not decay and
// matched no recording.
static const u8 ASC_STAIRS[4] = { 0x10, 0x3F, 0x01, 0x30 };

static unsigned RenderSynthesised (const u8 *pRom, const TRomChime *pChime,
                                   s16 *pStereo, unsigned nMaxFrames)
{
    const u8 *p = pRom + pChime->nOffset;
    const u16 nStep  = (u16) BE32 (p + 2);
    const u16 nDelay = (u16) BE32 (p + 6);
    const u16 nTotal = (u16) BE32 (p + 10);
    const u16 nCount = BE16 (p + 14);
    (void) nStep;           // folded into STEP_NS, which was fitted for this table

    u8 Table[512];
    for (unsigned i = 0; i < 512; i++)
    {
        Table[i] = ASC_STAIRS[(i / 64) % 4];
    }
    u32 Phase[4] = { 0, 0, 0, 0 };
    u32 Increment[4] = { 0, 0, 0, 0 };
    unsigned nVoices = 0;
    Increment[nVoices] = BE32 (p + 16);
    nVoices++;
    u16 nUntilNext = nDelay;

    // Chip samples are produced as the routine's time goes by; output frames are
    // interpolated between the last two chip samples. The high-pass stands in
    // for the machine's AC-coupled output, which the stairs' offset needs.
    u64 nTimeNs = 0;
    u64 nChip = 0;
    s32 nPrevious = 0, nCurrent = 0;
    unsigned j = 0;
    double fIn = 0.0, fOut = 0.0;
    const double fPole = 0.99715;           // 20 Hz at 44 100 Hz
    s32 nPeak = 1;
    unsigned nPointer = 0;

    for (u32 nStepIndex = 0; nStepIndex < nTotal && j < nMaxFrames; nStepIndex++)
    {
        const unsigned nNext = (nPointer + 1) & 0x1FF;
        Table[nNext] = (u8) (((unsigned) Table[nPointer] + Table[nNext]) >> 1);
        nPointer = nNext;

        nTimeNs += STEP_NS;
        while (nChip * 1000000000ull < nTimeNs * ASC_RATE && j < nMaxFrames)
        {
            s32 nSum = 0;
            for (unsigned v = 0; v < 4; v++)
            {
                Phase[v] += Increment[v];
                nSum += Table[(Phase[v] >> 15) & 0x1FF];
            }
            nPrevious = nCurrent;
            nCurrent = nSum;
            nChip++;

            // Every output frame at or before this chip sample.
            while ((u64) j * ASC_RATE <= nChip * ROM_CHIME_OUTPUT_RATE && j < nMaxFrames)
            {
                const double fT = (double) ((u64) j * ASC_RATE) / ROM_CHIME_OUTPUT_RATE
                                - (double) (nChip - 1);
                const double fX = nPrevious + (nCurrent - nPrevious) * fT;
                const double fY = fX - fIn + fPole * fOut;
                fIn = fX;
                fOut = j == 0 ? 0.0 : fY;
                const s32 nY = (s32) (fOut * 8.0);
                nPeak = nY > nPeak ? nY : (-nY > nPeak ? -nY : nPeak);
                pStereo[j * 2] = (s16) (nY > 32767 ? 32767 : nY < -32767 ? -32767 : nY);
                j++;
            }
        }

        if (nUntilNext == 0)
        {
            nUntilNext = 0xFFFF;
            if (nVoices < nCount)
            {
                Increment[nVoices] = BE32 (p + 16 + nVoices * 4);
                nVoices++;
                nUntilNext = nDelay;
            }
        }
        else
        {
            nUntilNext--;
        }
    }

    for (unsigned k = 0; k < j; k++)
    {
        const s16 v = (s16) ((s32) pStereo[k * 2] * HALF_SCALE / nPeak);
        pStereo[k * 2] = v;
        pStereo[k * 2 + 1] = v;
    }
    return j;
}

unsigned RomChimeRender (const u8 *pRom, u32 nRomBytes, const TRomChime *pChime,
                         s16 *pStereo, unsigned nMaxFrames)
{
    (void) nRomBytes;
    return pChime->Kind == RomChimeSynthesised
               ? RenderSynthesised (pRom, pChime, pStereo, nMaxFrames)
               : RenderSamples (pRom, pChime, pStereo, nMaxFrames);
}
