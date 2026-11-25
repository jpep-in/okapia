/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct QDMemoryRange
{
    uint32_t base;
    size_t size;
    uint8_t *data;

    uint8_t *Map (uint64_t address, size_t length) const;
};

struct QDMemory
{
    QDMemoryRange ram;
    QDMemoryRange screen;

    uint8_t *Pixels (uint64_t address, size_t length) const;
};

struct QDBlit
{
    const uint8_t *source;
    uint8_t *destination;
    unsigned sourcePitch, destinationPitch;
    unsigned rowBytes, height, pixelBytes;  // rowBytes: the bytes a destination row touches
    unsigned width, shift;      // Pixels in a row; a pixel is 1 << shift bits.
    // Where the first pixel starts in the byte the pointers above address,
    // counted from its most significant bit. Zero from 8 bits a pixel up.
    unsigned sourceBit, destinationBit;
    uint32_t pattern;           // Four bytes in guest order.
    uint8_t *patternRows;
    unsigned patternWidth, patternHeight, patternX, patternY;   // patternX in bytes from the row's start
    unsigned operation;
    uint32_t patternColor;      // What OR and BIC lay through a pattern.
    uint32_t flip;              // XORed into the source before anything else: the inverted modes.
    bool colorize;              // The source lays these two colours instead of itself.
    uint32_t foreColor, backColor;  // Four bytes in guest order, like the pattern.
    bool overlaps, reverse;
    bool serial;                // Goes through the one row of scratch: not for a second core.
    const uint8_t *inverseTable, *mask;
    unsigned sourceDepth, tableResolution, maskPitch, maskBit;
    unsigned scaleX, scaleY;
};

struct QDRegion
{
    const uint8_t *begin, *event, *end;
    int top, left, bottom, right;
};

enum class QDBlitReject
{
    Caller, Frame, Depth, Mode, Colorize, Alpha, PixMap, Rect, Pixels, Pattern, Overlap, Region, Count
};

struct QDBlitDiagnostic
{
    QDBlitReject reason;
    int mode = -1;             // No mode read before validating the frame.
    unsigned colorize = 0;      // The frame's BGnotWhite word.
};

// A StretchBits the System installs in place of the ROM's, recognised by
// FNV-1a fingerprints of its code from the entry through each call to a
// private transfer, so that the hooks can accept that call. Offsets are
// from the entry; a return offset is where the JSR through the trap table
// comes back, the fingerprint covering the entry up to it plus the four
// bytes that follow (the branch to the common exit).
struct QDStretchVariant
{
    const char *name;
    unsigned frameBytes;        // the LINK size: where the frame ends
    unsigned blitReturn, regionReturn, scaleReturn, scanEntry, finish;
    uint32_t blitCode, regionCode, scaleCode, scanCode;     // of entry .. return + 4
    uint32_t finishCode;        // of finish .. finish + QD_FINISH_BYTES
    uint32_t regionHandler;     // of the RgnBlt head the same System patch installs
};
constexpr unsigned QD_FINISH_BYTES = 0x50;
constexpr unsigned QD_REGION_HANDLER_BYTES = 4096;
extern const QDStretchVariant QDStretchVariants[];
extern const unsigned QDStretchVariantCount;

uint32_t QDFingerprint (const uint8_t *code, size_t length);
// Whether the bytes at the guest address have this fingerprint. Code is
// never at an odd address; an address outside the range does not match.
bool QDMatchesCode (const QDMemoryRange &ram, uint64_t address, size_t length, uint32_t expected);

// Which of the variants the StretchBits at this entry is, and which of its
// call sites the code verifies. One variant only: the first of which any
// call site matches, so that the sites never come from two routines.
struct QDStretchMatch
{
    const QDStretchVariant *variant;
    bool blit, region, scale, scan;
};
QDStretchMatch QDMatchStretch (const QDMemoryRange &ram, uint32_t entry,
                               const QDStretchVariant *variants, unsigned count);

// Decode the F1ACAD13 StretchBits frame at the rectangular BitBlt call.
bool QDPrepareBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                    QDBlit &blit, QDBlitDiagnostic *diagnostic = nullptr);
void QDRunBlit (const QDBlit &blit, unsigned first, unsigned end);
bool QDPrepareRegionBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                          QDBlit &blit, QDRegion (&regions)[3], int &top, int &left,
                          QDBlitDiagnostic *diagnostic = nullptr);
void QDRunRegionBlit (const QDBlit &blit, QDRegion (&regions)[3], int top, int left);
bool QDSlab8 (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
              unsigned operation, bool draw);
// Decode the verified ScaleBlt or stScanLoop call frame.
bool QDPrepareScanBlit (const QDMemory &memory, uint32_t frame, bool direct, bool systemScan,
                       QDBlit &blit, QDRegion (&regions)[3], int &top, int &left);
