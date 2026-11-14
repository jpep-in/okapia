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
    unsigned rowBytes, height, pixelBytes;
    uint32_t pattern;           // Four bytes in guest order.
    uint8_t *patternRows;
    unsigned patternWidth, patternHeight, patternX, patternY;
    unsigned operation;
    uint8_t patternColor;
    bool invert;
    bool overlaps, reverse;
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
    Caller, Frame, Depth, Mode, Colorize, Alpha, PixMap, Rect, Pixels, Pattern, Overlap, Count
};

struct QDBlitDiagnostic
{
    QDBlitReject reason;
    int mode = -1;             // No mode read before validating the frame.
    unsigned colorize = 0;
};

// Decode the F1ACAD13 StretchBits frame at the rectangular BitBlt call.
bool QDPrepareBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                    QDBlit &blit, QDBlitDiagnostic *diagnostic = nullptr);
void QDRunBlit (const QDBlit &blit, unsigned first, unsigned end);
bool QDPrepareRegionBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                          QDBlit &blit, QDRegion (&regions)[3], int &top, int &left);
void QDRunRegionBlit (const QDBlit &blit, QDRegion (&regions)[3], int top, int left);
bool QDSlab8 (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
              unsigned operation, bool draw);
// Decode the verified ScaleBlt or stScanLoop call frame.
bool QDPrepareScanBlit (const QDMemory &memory, uint32_t frame, bool direct, bool systemScan,
                       QDBlit &blit, QDRegion (&regions)[3], int &top, int &left);
