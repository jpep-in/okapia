/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "quickdraw_blit.h"

#include <string.h>

uint8_t *QDMemoryRange::Map (uint64_t address, size_t length) const
{
    if (!data || address < base || address > UINT32_MAX)
        return nullptr;
    const uint64_t offset = address - base;
    if (offset >= size || length > size - offset || length > UINT64_C(0x100000000) - address)
        return nullptr;
    return data + offset;
}

uint8_t *QDMemory::Pixels (uint64_t address, size_t length) const
{
    uint8_t *ptr = ram.Map (address, length);
    return ptr ? ptr : screen.Map (address, length);
}

namespace
{
unsigned Word (const uint8_t *p)
{
    return (unsigned (p[0]) << 8) | p[1];
}

uint32_t Long (const uint8_t *p)
{
    return (uint32_t (Word (p)) << 16) | Word (p + 2);
}

struct Rect
{
    int top, left, bottom, right;

    int Width () const { return right - left; }
    int Height () const { return bottom - top; }
    bool Contains (const Rect &r) const
    {
        return r.top >= top && r.left >= left && r.bottom <= bottom && r.right <= right;
    }
};

Rect ReadRect (const uint8_t *p)
{
    return {int16_t (Word (p)), int16_t (Word (p + 2)),
            int16_t (Word (p + 4)), int16_t (Word (p + 6))};
}

uint8_t *MapRect (const QDMemory &memory, const uint8_t *pixmap, const Rect &rect,
                  unsigned pixelBytes, unsigned &pitch, size_t &span)
{
    const Rect bounds = ReadRect (pixmap + 6);
    pitch = Word (pixmap + 4) & 0x7fff;
    if (!bounds.Contains (rect) || unsigned (bounds.Width ()) * pixelBytes > pitch)
        return nullptr;
    const uint64_t first = uint64_t (Long (pixmap))
                         + unsigned (rect.top - bounds.top) * uint64_t (pitch)
                         + unsigned (rect.left - bounds.left) * pixelBytes;
    span = size_t (rect.Height () - 1) * pitch + unsigned (rect.Width ()) * pixelBytes;
    return memory.Pixels (first, span);
}
}

bool QDPrepareBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                    QDBlit &blit, QDBlitDiagnostic *diagnostic)
{
    using Reject = QDBlitReject;
    const auto reject = [diagnostic] (Reject reason) {
        if (diagnostic)
            diagnostic->reason = reason;
        return false;
    };
    // Offsets from the exact ROM's StretchBits, not the public PixMap ABI.
    constexpr unsigned frameBytes = 0x2c8;
    if (a[6] < frameBytes || a[4] != a[6] - 0x168 || a[5] != a[6] - 0xf4)
        return reject (Reject::Frame);
    const auto *frame = memory.ram.Map (a[6] - frameBytes, frameBytes);
    if (!frame)
        return reject (Reject::Frame);
    const auto *fp = frame + frameBytes;
    const unsigned mode = Word (fp - 0x98);
    const unsigned shift = uint16_t (d[4]);
    const unsigned colorize = Word (fp - 0x104);
    if (diagnostic)
    {
        diagnostic->mode = int (mode);
        diagnostic->colorize = colorize;
    }
    if (shift < 3 || shift > 5)
        return reject (Reject::Depth);
    if (shift == 3 ? mode > 15 : mode != 0 && mode != 8)
        return reject (Reject::Mode);
    if (colorize)
        return reject (Reject::Colorize);
    if (shift > 3 && fp[-0x92]) // alphaMode
        return reject (Reject::Alpha);

    const unsigned pixelBytes = 1u << (shift - 3);
    const auto *srcPix = fp - 0x168;
    const auto *dstPix = fp - 0xf4;
    if (Word (dstPix + 32) != pixelBytes * 8)
        return reject (Reject::PixMap);
    const auto *dstRect = memory.ram.Map (a[3], 8);
    if (!dstRect)
        return reject (Reject::Rect);

    const Rect clipped = ReadRect (fp - 0x74);
    const Rect dest = ReadRect (dstRect);
    if (clipped.Width () <= 0 || clipped.Height () <= 0 || !dest.Contains (clipped))
        return reject (Reject::Rect);

    blit = {};
    blit.pixelBytes = pixelBytes;
    blit.operation = mode & 3;
    blit.rowBytes = unsigned (clipped.Width ()) * pixelBytes;
    blit.height = unsigned (clipped.Height ());
    size_t destinationSpan;
    blit.destination = MapRect (memory, dstPix, clipped, pixelBytes,
                                blit.destinationPitch, destinationSpan);
    if (!blit.destination)
        return reject (Reject::Pixels);

    if (mode & 8)
    {
        unsigned rowBytes = Word (fp - 0x8e);
        unsigned rows = 16;
        if (rowBytes)
        {
            if (pixelBytes != 1 || rowBytes < 4 || (rowBytes & (rowBytes - 1)) ||
                Word (fp - 0x8c) != rowBytes - 4)
                return reject (Reject::Pattern);
            const unsigned size = Word (fp - 0x8a) + 1;
            rows = size / rowBytes;
            if (!rows || rows * rowBytes != size || (rows & (rows - 1)))
                return reject (Reject::Pattern);
        }
        else
            rowBytes = 4;
        auto *expanded = memory.ram.Map (Long (fp - 0x88), rows * rowBytes);
        if (!expanded)
            return reject (Reject::Pattern);
        const uint32_t pattern = Long (expanded);
        bool solid = rowBytes == 4;
        for (unsigned row = 1; solid && row < rows; ++row)
            solid = Long (expanded + row * rowBytes) == pattern;
        solid = solid && (pixelBytes != 1 || (pattern & 0xff) * 0x01010101u == pattern) &&
                (pixelBytes != 2 || uint16_t (pattern) == pattern >> 16);
        if (!solid && pixelBytes != 1)
            return reject (Reject::Pattern);
        blit.pattern = pattern;
        blit.patternColor = uint8_t (Long (fp - (blit.operation == 1 ? 0xa0 : 0xa4)));
        if (!solid || blit.operation)
        {
            const auto pat = reinterpret_cast<uintptr_t> (expanded);
            const auto dst = reinterpret_cast<uintptr_t> (blit.destination);
            if (pat < dst + destinationSpan && dst < pat + rows * rowBytes)
                return reject (Reject::Overlap);
            // PatExpand has already applied the inversion for pattern modes.
            blit.patternRows = expanded;
            blit.patternWidth = rowBytes;
            blit.patternHeight = rows;
            blit.patternX = unsigned (clipped.left - ReadRect (dstPix + 6).left);
            blit.patternY = unsigned (clipped.top);
        }
        return true;
    }

    const auto *srcRect = memory.ram.Map (a[2], 8);
    if (!srcRect)
        return reject (Reject::Rect);
    if (uint16_t (d[3]) != shift)
        return reject (Reject::Depth);
    if (d[7] != 0 && (pixelBytes != 1 || d[7] != UINT32_MAX))
        return reject (Reject::Mode);
    if (Word (fp - 0x8e))
        return reject (Reject::Pattern);
    if (memcmp (srcPix + 30, dstPix + 30, 8) != 0)
        return reject (Reject::PixMap);
    // StretchBits already checked indexed colour table seeds and selected
    // BitBlt. Reject a different depth or any surviving conversion flags.
    const Rect src = ReadRect (srcRect);
    if (src.Width () != dest.Width () || src.Height () != dest.Height ())
        return reject (Reject::Rect);
    const Rect source = {src.top + clipped.top - dest.top, src.left + clipped.left - dest.left,
                         src.top + clipped.bottom - dest.top, src.left + clipped.right - dest.left};
    size_t sourceSpan;
    blit.source = MapRect (memory, srcPix, source, pixelBytes, blit.sourcePitch, sourceSpan);
    if (!blit.source)
        return reject (Reject::Pixels);
    const auto srcAddress = reinterpret_cast<uintptr_t> (blit.source);
    const auto dstAddress = reinterpret_cast<uintptr_t> (blit.destination);
    blit.overlaps = srcAddress < dstAddress + destinationSpan && dstAddress < srcAddress + sourceSpan;
    // Match the ROM's overlap contract: one surface with a single pitch.
    if (blit.overlaps && (Long (srcPix) != Long (dstPix) || blit.sourcePitch != blit.destinationPitch))
        return reject (Reject::Overlap);
    blit.reverse = blit.overlaps && dstAddress > srcAddress;
    blit.invert = d[7] != 0;
    return true;
}

static void Apply8 (uint8_t &dst, unsigned src, unsigned operation)
{
    switch (operation)
    {
    case 0: dst = src; break;
    case 1: dst |= src; break;
    case 2: dst ^= src; break;
    case 3: dst &= ~src; break;
    }
}

static void RunSpan (const QDBlit &blit, unsigned y, unsigned left, unsigned right)
{
    const uint32_t pattern = __builtin_bswap32 (blit.pattern);
    auto *dst = blit.destination + size_t (y) * blit.destinationPitch + left;
    const unsigned length = right - left;
    if (blit.sourceDepth == 32)
    {
        const auto *src = blit.source + size_t (y) * blit.sourcePitch + 4 * left;
        const unsigned bits = blit.tableResolution;
        const unsigned shift = 8 - bits;
        for (unsigned i = 0; i < length; ++i, src += 4)
        {
            const unsigned index = (unsigned (src[1] >> shift) << (2 * bits)) |
                                   (unsigned (src[2] >> shift) << bits) | (src[3] >> shift);
            dst[i] = blit.inverseTable[index];
        }
    }
    else if (blit.sourceDepth == 8)
    {
        const auto *src = blit.source + size_t (y / blit.scaleY) * blit.sourcePitch;
        const auto *mask = blit.mask ? blit.mask + size_t (y) * blit.maskPitch : nullptr;
        for (unsigned x = left; x < right; ++x)
        {
            const unsigned maskBit = blit.maskBit + x;
            if (!mask || (mask[maskBit / 8] & (0x80u >> (maskBit % 8))))
                dst[x - left] = src[x / blit.scaleX];
        }
    }
    else if (blit.source)
    {
        const auto *src = blit.source + size_t (y) * blit.sourcePitch + left;
        if (blit.operation || blit.invert)
        {
            const unsigned flip = blit.invert ? 0xff : 0;
            for (unsigned i = 0; i < length; ++i)
            {
                const unsigned x = blit.reverse ? length - i - 1 : i;
                Apply8 (dst[x], src[x] ^ flip, blit.operation);
            }
        }
        else if (blit.overlaps)
            memmove (dst, src, length);
        else
            memcpy (dst, src, length);
    }
    else if (blit.patternRows)
    {
        const auto *row = blit.patternRows +
            ((blit.patternY + y) & (blit.patternHeight - 1)) * blit.patternWidth;
        for (unsigned x = 0; x < length; ++x)
        {
            const unsigned src = row[(blit.patternX + left + x) & (blit.patternWidth - 1)];
            if (blit.operation & 1)
                dst[x] = (dst[x] & ~src) | (blit.patternColor & src);
            else
                Apply8 (dst[x], src, blit.operation);
        }
    }
    else if (blit.pixelBytes == 1)
        memset (dst, int (blit.pattern & 0xff), length);
    else
    {
        unsigned x = 0;
        for (; x + 4 <= length; x += 4)
            memcpy (dst + x, &pattern, 4);
        if (x < length)
            memcpy (dst + x, &pattern, 2);
    }
}

void QDRunBlit (const QDBlit &blit, unsigned first, unsigned end)
{
    for (unsigned row = first; row < end; ++row)
    {
        const unsigned y = blit.reverse ? blit.height - 1 - row : row;
        RunSpan (blit, y, 0, blit.rowBytes);
    }
}

static const uint8_t *ToggleRegionEdges (uint64_t *edges, const uint8_t *event,
                                         unsigned width, int left)
{
    auto *p = event + 2;
    while (Word (p) != 0x7fff)
    {
        const int x = int16_t (Word (p)) - left;
        p += 2;
        // Fold all endpoints at or before the left edge into X=0.
        if (x < int (width))
        {
            const unsigned bit = x > 0 ? unsigned (x) : 0;
            edges[bit / 64] ^= UINT64_C(1) << (bit % 64);
        }
    }
    return p + 2;
}

static bool PrepareRegions (const QDMemory &memory, const uint8_t *args, const Rect &clip,
                            const QDBlit &blit, QDRegion (&regions)[3], int &top, int &left)
{
    top = clip.top;
    left = clip.left;
    const auto dst = reinterpret_cast<uintptr_t> (blit.destination);
    const auto dstEnd = dst + size_t (blit.height - 1) * blit.destinationPitch + blit.rowBytes;
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto *handle = memory.ram.Map (Long (args + i * 4), 4);
        const auto *header = handle ? memory.ram.Map (Long (handle), 10) : nullptr;
        if (!header)
            return false;
        const unsigned size = Word (header);
        if (size < 10 || (size & 1) || !memory.ram.Map (Long (handle), size))
            return false;
        const Rect bounds = ReadRect (header + 2);
        auto &r = regions[i];
        r = {nullptr, nullptr, nullptr, bounds.top, bounds.left, bounds.bottom, bounds.right};
        if (size == 10)
            continue;
        const auto addr = reinterpret_cast<uintptr_t> (header);
        if (addr < dstEnd && dst < addr + size)
            return false;
        const auto *p = header + 10;
        const auto *end = header + size;
        int lastY = -32769;
        while (p < end && Word (p) != 0x7fff)
        {
            const int y = int16_t (Word (p));
            p += 2;
            if (y <= lastY || y < bounds.top || y > bounds.bottom)
                return false;
            lastY = y;
            int lastX = -32769;
            unsigned count = 0;
            while (p < end && Word (p) != 0x7fff)
            {
                const int x = int16_t (Word (p));
                if (x <= lastX || x < bounds.left || x > bounds.right)
                    return false;
                lastX = x;
                p += 2;
                ++count;
            }
            if (p == end || (count & 1))
                return false;
            p += 2;
        }
        if (end - p != 2)
            return false;
        r.begin = r.event = header + 10;
        r.end = p;
    }
    return true;
}

bool QDPrepareRegionBlit (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
                         QDBlit &blit, QDRegion (&regions)[3], int &top, int &left)
{
    const auto *args = memory.ram.Map (uint64_t (a[6]) + 10, 30);
    if (!args || uint16_t (d[4]) != 3 || a[6] < 0x168)
        return false;
    // RgnBlt reloads the original rectangles after the dispatch.
    uint32_t addresses[8];
    memcpy (addresses, a, sizeof addresses);
    addresses[2] = Long (args + 0x24 - 10);
    addresses[3] = Long (args + 0x1c - 10);
    addresses[4] = a[6] - 0x168;
    addresses[5] = a[6] - 0xf4;
    return QDPrepareBlit (memory, addresses, d, blit) &&
           PrepareRegions (memory, args, ReadRect (memory.ram.Map (a[6] - 0x74, 8)),
                           blit, regions, top, left);
}

static bool Disjoint (const void *a, size_t alen, const void *b, size_t blen)
{
    const auto x = reinterpret_cast<uintptr_t> (a);
    const auto y = reinterpret_cast<uintptr_t> (b);
    return x + alen <= y || y + blen <= x;
}

static const uint8_t *MapStencil (const QDMemory &memory, const uint8_t *pix, const Rect &rect,
                                  unsigned &pitch, unsigned &bit, size_t &size)
{
    const Rect bounds = ReadRect (pix + 6);
    pitch = Word (pix + 4) & 0x7fff;
    if (!bounds.Contains (rect) || (unsigned (bounds.Width ()) + 7) / 8 > pitch)
        return nullptr;
    const unsigned x = unsigned (rect.left - bounds.left);
    const uint64_t address = uint64_t (Long (pix)) +
        uint64_t (rect.top - bounds.top) * pitch + x / 8;
    bit = x % 8;
    size = size_t (rect.Height () - 1) * pitch + (bit + rect.Width () + 7) / 8;
    return memory.Pixels (address, size);
}

static const uint8_t *InverseTable (const QDMemory &memory, uint32_t palette,
                                    unsigned &resolution, size_t &entries)
{
    const auto dereference = [&] (uint32_t handle, size_t size) {
        const auto *pointer = memory.ram.Map (handle, 4);
        const uint32_t address = pointer && handle ? Long (pointer) : 0;
        return address ? memory.ram.Map (address, size) : nullptr;
    };
    const auto *deviceHandle = memory.ram.Map (0xcc8, 4);  // TheGDevice
    const auto *device = deviceHandle ? dereference (Long (deviceHandle), 0x1a) : nullptr;
    if (!device)
        return nullptr;
    const auto *devicePix = dereference (Long (device + 0x16), 46);
    const auto *colors = dereference (palette, 8);
    const auto *inverse = dereference (Long (device + 6), 6);
    if (!devicePix || Long (devicePix + 42) != palette || !colors || !inverse ||
        Long (colors) != Long (inverse))
        return nullptr;
    resolution = Word (inverse + 4);
    if (resolution < 3 || resolution > 5)
        return nullptr;
    entries = 1u << (3 * resolution);
    inverse = dereference (Long (device + 6), 6 + entries + 4);
    // The signed flag after the cube selects the guest luminance loops.
    if (!inverse || (Word (inverse + 6 + entries + 2) & 0x8000))
        return nullptr;
    return inverse + 6;
}

bool QDPrepareScanBlit (const QDMemory &memory, uint32_t frame, bool direct, bool systemScan,
                           QDBlit &blit, QDRegion (&regions)[3], int &top, int &left)
{
    const unsigned frameBytes = systemScan ? 0x308 : 0x2c8;
    const auto *body = frame >= frameBytes ? memory.ram.Map (frame - frameBytes, frameBytes + 52) : nullptr;
    if (!body)
        return false;
    const auto *fp = body + frameBytes;
    const auto *src = fp - 0x168;
    const auto *dst = fp - 0xf4;
    const bool masked = Long (fp + 0x2c) != 0;
    if (Word (fp - 0x98) || fp[-0xa5] || Word (dst + 32) != 8 || Word (dst + 30) ||
        Word (src + 30) != (direct ? 16 : 0) || (masked && direct))
        return false;
    const unsigned depth = Word (src + 32);
    if (depth != (direct ? 32u : 8u) ||
        (direct && (Word (src + 34) != 3 || Word (src + 36) != 8)))
        return false;
    const auto *srcRect = memory.ram.Map (Long (fp + 0x24), 8);
    const auto *dstRect = memory.ram.Map (Long (fp + 0x1c), 8);
    if (!srcRect || !dstRect)
        return false;
    const Rect clip = ReadRect (fp - 0x74);
    const Rect d = ReadRect (dstRect), s = ReadRect (srcRect);
    if (clip.Width () <= 0 || clip.Height () <= 0 || !d.Contains (clip) ||
        s.Width () <= 0 || s.Height () <= 0)
        return false;
    const auto translate = [&] (const Rect &r) {
        return Rect {r.top + clip.top - d.top, r.left + clip.left - d.left,
                     r.top + clip.bottom - d.top, r.left + clip.right - d.left};
    };

    blit = {};
    blit.pixelBytes = 1;
    blit.sourceDepth = depth;
    blit.scaleX = blit.scaleY = 1;
    blit.rowBytes = unsigned (clip.Width ());
    blit.height = unsigned (clip.Height ());
    const bool resize = s.Width () != d.Width () || s.Height () != d.Height ();
    if (resize)
    {
        // QuickDraw's exact power-of-two expansion repeats each input pixel.
        // Limit this case to full rectangles, where both sampling origins
        // match the rectangle supplied by the caller.
        if (direct || masked || !clip.Contains (d) || d.Width () % s.Width () ||
            d.Height () % s.Height ())
            return false;
        blit.scaleX = d.Width () / s.Width ();
        blit.scaleY = d.Height () / s.Height ();
        const auto supported = [] (unsigned scale) { return scale == 1 || scale == 2 || scale == 4; };
        if (!supported (blit.scaleX) || !supported (blit.scaleY))
            return false;
    }
    else if (!direct && !masked)
        return false;
    size_t srcSize, dstSize;
    blit.destination = MapRect (memory, dst, clip, 1, blit.destinationPitch, dstSize);
    blit.source = MapRect (memory, src, resize ? s : translate (s), depth / 8,
                           blit.sourcePitch, srcSize);
    if (!blit.destination || !blit.source || !Disjoint (blit.source, srcSize, blit.destination, dstSize))
        return false;

    if (direct)
    {
        size_t entries;
        blit.inverseTable = InverseTable (memory, Long (dst + 42), blit.tableResolution, entries);
        if (!blit.inverseTable || !Disjoint (blit.inverseTable, entries, blit.destination, dstSize))
            return false;
    }
    else
    {
        const uint32_t palette = Long (src + 42);
        const unsigned flags = Word (fp - (systemScan ? 0x2d4 : 0x104));
        if (!palette || palette != Long (dst + 42) ||
            (flags & 0x399) || Long (fp - 0x1c8))
            return false;
        if (masked)
        {
            const auto *mask = fp - 0x1b8;
            const auto *maskRect = memory.ram.Map (Long (fp + 0x20), 8);
            if (Word (mask + 32) != 1 || !maskRect)
                return false;
            const Rect m = ReadRect (maskRect);
            if (m.Width () != d.Width () || m.Height () != d.Height ())
                return false;
            size_t maskSize;
            blit.mask = MapStencil (memory, mask, translate (m), blit.maskPitch, blit.maskBit, maskSize);
            if (!blit.mask || !Disjoint (blit.mask, maskSize, blit.destination, dstSize))
                return false;
        }
    }
    return PrepareRegions (memory, fp + 10, clip, blit, regions, top, left);
}

void QDRunRegionBlit (const QDBlit &blit, QDRegion (&regions)[3], int top, int left)
{
    // A region encodes XOR changes to its horizontal endpoints at each Y.
    // Row bytes are limited to 0x7fff. This scratch belongs to the guest core.
    static uint64_t edges[3][512];
    static uint8_t sourceRow[0x7fff];
    QDBlit span = blit;
    if (blit.overlaps)
    {
        span.source = sourceRow;
        span.sourcePitch = 0;
        span.reverse = false;
    }
    memset (edges, 0, sizeof edges);
    if (blit.reverse)
        for (unsigned n = 0; n < 3; ++n)
        {
            auto &r = regions[n];
            for (auto *event = r.begin; event != r.end;)
                event = ToggleRegionEdges (edges[n], event, blit.rowBytes, left);
            r.event = r.end;
        }
    for (unsigned line = 0; line < blit.height; ++line)
    {
        const unsigned row = blit.reverse ? blit.height - 1 - line : line;
        const int y = top + int (row);
        for (unsigned n = 0; n < 3; ++n)
        {
            auto &r = regions[n];
            while (r.event != (blit.reverse ? r.begin : r.end))
            {
                auto *event = r.event;
                if (blit.reverse)
                {
                    event -= 2; // Previous line's end marker.
                    do
                    {
                        event -= 2;
                    }
                    while (event != r.begin && Word (event - 2) != 0x7fff);
                }
                const int eventY = int16_t (Word (event));
                if (blit.reverse ? eventY <= y : eventY > y)
                    break;
                const auto *next = ToggleRegionEdges (edges[n], event, blit.rowBytes, left);
                r.event = blit.reverse ? event : next;
            }
        }
        unsigned start = 0;
        bool drawing = false;
        bool active[3] = {};
        unsigned clipLeft = 0, clipRight = blit.rowBytes;
        for (const auto &r : regions)
        {
            if (y < r.top || y >= r.bottom)
            {
                clipRight = 0;
                break;
            }
            if (!r.end)
            {
                if (r.left > left + int (clipLeft))
                    clipLeft = unsigned (r.left - left);
                if (r.right < left + int (clipRight))
                    clipRight = r.right > left ? unsigned (r.right - left) : 0;
            }
        }
        if (clipLeft >= clipRight)
            continue;
        if (blit.overlaps)
            memcpy (sourceRow, blit.source + size_t (row) * blit.sourcePitch, blit.rowBytes);
        for (unsigned n = 0; n < 3; ++n)
            active[n] = !regions[n].end;
        drawing = active[0] && active[1] && active[2];
        for (unsigned word = 0; 64 * word < clipRight; ++word)
        {
            uint64_t events = edges[0][word] | edges[1][word] | edges[2][word];
            while (events)
            {
                const unsigned bit = __builtin_ctzll (events);
                const unsigned x = 64 * word + bit;
                if (x >= clipRight)
                    break;
                events &= events - 1;
                for (unsigned n = 0; n < 3; ++n)
                    active[n] ^= (edges[n][word] >> bit) & 1;
                const bool visible = active[0] && active[1] && active[2];
                if (visible && !drawing)
                    start = x > clipLeft ? x : clipLeft;
                if (drawing && !visible && start < x)
                    RunSpan (span, row, start, x);
                drawing = visible;
            }
        }
        if (drawing)
            RunSpan (span, row, start > clipLeft ? start : clipLeft, clipRight);
    }
}

bool QDSlab8 (const QDMemory &memory, const uint32_t *a, const uint32_t *d,
              unsigned operation, bool draw)
{
    const auto *minimum = memory.ram.Map (a[3], 8);
    if (!minimum)
        return false;
    const Rect clip = ReadRect (minimum);
    const int start = int16_t (d[1]);
    const int end = int16_t (d[2]);
    const int left = start > clip.left ? start : clip.left;
    const int right = end < clip.right ? end : clip.right;
    if (left < 0 || left >= right)
        return false;

    auto *pixels = memory.Pixels (uint64_t (a[1]) + unsigned (left), right - left);
    const auto *mask = memory.ram.Map (uint64_t (a[2]) + unsigned (left), right - left);
    if (!pixels || !mask)
        return false;
    const auto dst = reinterpret_cast<uintptr_t> (pixels);
    const auto src = reinterpret_cast<uintptr_t> (mask);
    const unsigned size = unsigned (right - left);
    if (src < dst + size && dst < src + size)
        return false;
    if (!draw)
        return true;
    for (unsigned x = 0; x < size; ++x)
    {
        const unsigned shift = 24 - ((unsigned (left) + x) & 3) * 8;
        const unsigned pattern = uint8_t (d[6] >> shift);
        unsigned gate = mask[x];
        if (operation)
            gate &= pattern;
        if (operation == 2)
            pixels[x] ^= gate;
        else
        {
            const unsigned color = operation ? uint8_t (d[operation == 1 ? 4 : 5] >> shift) : pattern;
            pixels[x] = (pixels[x] & ~gate) | (color & gate);
        }
    }
    return true;
}
