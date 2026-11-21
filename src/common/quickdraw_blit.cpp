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

// The bytes that hold a rectangle of pixels of 1 << shift bits, and where its
// first pixel starts in the first of them.
uint8_t *MapBits (const QDMemory &memory, const uint8_t *pixmap, const Rect &rect,
                  unsigned shift, unsigned &pitch, size_t &span, unsigned &bit)
{
    const Rect bounds = ReadRect (pixmap + 6);
    pitch = Word (pixmap + 4) & 0x7fff;
    if (!bounds.Contains (rect) || ((unsigned (bounds.Width ()) << shift) + 7) / 8 > pitch)
        return nullptr;
    const unsigned left = unsigned (rect.left - bounds.left) << shift;
    const uint64_t first = uint64_t (Long (pixmap))
                         + unsigned (rect.top - bounds.top) * uint64_t (pitch) + left / 8;
    bit = left % 8;
    span = size_t (rect.Height () - 1) * pitch + (bit + (unsigned (rect.Width ()) << shift) + 7) / 8;
    return memory.Pixels (first, span);
}

uint8_t *MapRect (const QDMemory &memory, const uint8_t *pixmap, const Rect &rect,
                  unsigned pixelBytes, unsigned &pitch, size_t &span)
{
    unsigned bit;
    return MapBits (memory, pixmap, rect, pixelBytes == 4 ? 5 : pixelBytes == 2 ? 4 : 3, pitch, span, bit);
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
    // BGnotWhite. StretchBits sets it when the transfer goes through other
    // colours than black on white, or inverts its source; BitBlt and RgnBlt
    // then take their colour loops for copy, OR and BIC. Their dispatch never
    // looks at it for XOR or for a pattern mode, where it is left over.
    const unsigned colorLoops = Word (fp - 0x104);
    if (diagnostic)
    {
        diagnostic->mode = int (mode);
        diagnostic->colorize = colorLoops;
    }
    if (shift > 5)
        return reject (Reject::Depth);
    if (mode > 15)
        return reject (Reject::Mode);
    // alphaMode. The loops the System has put in the trap table in place of
    // the ROM's (pattern copy, for BitBlt and RgnBlt) differ from them only
    // when it is set, which ColorMap allows at 16 and 32 bits alone.
    if (fp[-0x92])
        return reject (Reject::Alpha);

    const auto *srcPix = fp - 0x168;
    const auto *dstPix = fp - 0xf4;
    if (Word (dstPix + 32) != 1u << shift)
        return reject (Reject::PixMap);
    const auto *dstRect = memory.ram.Map (a[3], 8);
    if (!dstRect)
        return reject (Reject::Rect);

    const Rect clipped = ReadRect (fp - 0x74);
    const Rect dest = ReadRect (dstRect);
    if (clipped.Width () <= 0 || clipped.Height () <= 0 || !dest.Contains (clipped))
        return reject (Reject::Rect);

    blit = {};
    blit.shift = shift;
    blit.pixelBytes = shift > 3 ? 1u << (shift - 3) : 1;
    blit.operation = mode & 3;
    blit.width = unsigned (clipped.Width ());
    blit.height = unsigned (clipped.Height ());
    size_t destinationSpan;
    blit.destination = MapBits (memory, dstPix, clipped, shift, blit.destinationPitch,
                                destinationSpan, blit.destinationBit);
    if (!blit.destination)
        return reject (Reject::Pixels);
    blit.rowBytes = (blit.destinationBit + (blit.width << shift) + 7) / 8;
    // QuickDraw's loops work on longs, laid from the start of the row. A long
    // of colour, of solid pattern or of inversion that holds one value for
    // every pixel is the same wherever a span starts, and that is all the
    // writers below have to know: anything else stays with the guest.
    const auto uniform = [shift] (uint32_t value) {
        return shift == 5 || (uint16_t (value) == value >> 16 &&
               (shift == 4 || (value & 0xff) * 0x01010101u == value));
    };

    if (mode & 8)
    {
        unsigned rowBytes = Word (fp - 0x8e);
        unsigned rows = 16;
        if (rowBytes)
        {
            if (rowBytes < 4 || (rowBytes & (rowBytes - 1)) || Word (fp - 0x8c) != rowBytes - 4)
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
        bool solid = rowBytes == 4 && uniform (pattern);
        for (unsigned row = 1; solid && row < rows; ++row)
            solid = Long (expanded + row * rowBytes) == pattern;
        blit.pattern = pattern;
        if (!solid || blit.operation)
        {
            const auto pat = reinterpret_cast<uintptr_t> (expanded);
            const auto dst = reinterpret_cast<uintptr_t> (blit.destination);
            if (pat < dst + destinationSpan && dst < pat + rows * rowBytes)
                return reject (Reject::Overlap);
            if (blit.operation & 1)
            {
                // OR lays the foreground through the pattern, BIC the background.
                blit.patternColor = Long (fp - (blit.operation == 1 ? 0xa0 : 0xa4));
                if (!uniform (blit.patternColor))
                    return reject (Reject::Pattern);
            }
            // PatExpand has already applied the inversion for pattern modes.
            blit.patternRows = expanded;
            blit.patternWidth = rowBytes;
            blit.patternHeight = rows;
            blit.patternX = (unsigned (clipped.left - ReadRect (dstPix + 6).left) << shift) / 8;
            blit.patternY = unsigned (clipped.top);
        }
        return true;
    }

    const auto *srcRect = memory.ram.Map (a[2], 8);
    if (!srcRect)
        return reject (Reject::Rect);
    if (uint16_t (d[3]) != shift)
        return reject (Reject::Depth);
    if (!uniform (d[7]))
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
    blit.source = MapBits (memory, srcPix, source, shift, blit.sourcePitch, sourceSpan, blit.sourceBit);
    if (!blit.source)
        return reject (Reject::Pixels);
    const auto srcAddress = reinterpret_cast<uintptr_t> (blit.source);
    const auto dstAddress = reinterpret_cast<uintptr_t> (blit.destination);
    blit.overlaps = srcAddress < dstAddress + destinationSpan && dstAddress < srcAddress + sourceSpan;
    // Match the ROM's overlap contract: one surface with a single pitch.
    if (blit.overlaps && (Long (srcPix) != Long (dstPix) || blit.sourcePitch != blit.destinationPitch))
        return reject (Reject::Overlap);
    blit.reverse = blit.overlaps && dstAddress > srcAddress;
    blit.flip = d[7];
    // Below 8 bits a source that does not start at the same bit of its byte
    // is shifted into the row of scratch first.
    blit.serial = blit.sourceBit != blit.destinationBit;
    if (colorLoops && blit.operation != 2)
    {
        // The colour loops lay the foreground where the source has a one,
        // and the background where it has a zero (copy) or nothing (OR); BIC
        // lays the background where it has a one.
        const uint32_t fore = Long (fp - 0xa0), back = Long (fp - 0xa4);
        const bool plain = blit.operation == 0 ? fore == UINT32_MAX && back == 0
                         : blit.operation == 1 ? fore == UINT32_MAX : back == 0;
        if (!plain)
        {
            if (!uniform (fore) || !uniform (back))
                return reject (Reject::Colorize);
            blit.colorize = true;
            blit.foreColor = fore;
            blit.backColor = back;
        }
    }
    return true;
}

// The pixel loops below work on eight bytes at a time through memcpy, which
// compiles to one unaligned load or store. Guest pixels are ordinary memory:
// the Device memory rule of the output frame buffer does not apply to them.
namespace
{
inline uint64_t Load64 (const uint8_t *p)
{
    uint64_t value;
    memcpy (&value, p, 8);
    return value;
}

inline void Store64 (uint8_t *p, uint64_t value)
{
    memcpy (p, &value, 8);
}

// The kernel's memcpy goes byte by byte unless both ends are aligned on
// eight and the length reaches 128, and its memmove does whenever it has to
// run backwards: a row of pixels is rarely that lucky. These three do not
// care about alignment, and must not be turned back into library calls.
#if defined (__GNUC__) && !defined (__clang__)
#define QD_LOOP __attribute__ ((optimize ("no-tree-loop-distribute-patterns")))
#else
#define QD_LOOP
#endif

// Ascending: right for separate ranges and for a destination below its source.
QD_LOOP void CopyUp (uint8_t *dst, const uint8_t *src, size_t length)
{
    size_t i = 0;
    for (; i + 16 <= length; i += 16)
    {
        const uint64_t low = Load64 (src + i), high = Load64 (src + i + 8);
        Store64 (dst + i, low);
        Store64 (dst + i + 8, high);
    }
    if (i + 8 <= length)
    {
        Store64 (dst + i, Load64 (src + i));
        i += 8;
    }
    for (; i < length; ++i)
        dst[i] = src[i];
}

// Descending: for a destination above its source.
QD_LOOP void CopyDown (uint8_t *dst, const uint8_t *src, size_t length)
{
    size_t i = length;
    for (; i >= 16; i -= 16)
    {
        const uint64_t low = Load64 (src + i - 16), high = Load64 (src + i - 8);
        Store64 (dst + i - 8, high);
        Store64 (dst + i - 16, low);
    }
    if (i >= 8)
    {
        Store64 (dst + i - 8, Load64 (src + i - 8));
        i -= 8;
    }
    while (i)
    {
        --i;
        dst[i] = src[i];
    }
}

QD_LOOP void Fill (uint8_t *dst, uint64_t eight, size_t length)
{
    size_t i = 0;
    for (; i + 16 <= length; i += 16)
    {
        Store64 (dst + i, eight);
        Store64 (dst + i + 8, eight);
    }
    if (i + 8 <= length)
    {
        Store64 (dst + i, eight);
        i += 8;
    }
    for (; i < length; ++i)
        dst[i] = uint8_t (eight >> (i & 7) * 8);
}

// One of QuickDraw's four boolean transfers, on any number of whole bytes.
inline uint64_t Transfer (uint64_t dst, uint64_t src, unsigned operation)
{
    switch (operation)
    {
    case 0: return src;
    case 1: return dst | src;
    case 2: return dst ^ src;
    default: return dst & ~src;
    }
}

// dst = transfer (dst, src ^ flip). The two ranges must not overlap.
template <unsigned Operation>
void Combine (uint8_t *dst, const uint8_t *src, size_t length, uint64_t flip)
{
    size_t i = 0;
    for (; i + 8 <= length; i += 8)
        Store64 (dst + i, Transfer (Load64 (dst + i), Load64 (src + i) ^ flip, Operation));
    for (; i < length; ++i)
        dst[i] = uint8_t (Transfer (dst[i], src[i] ^ uint8_t (flip >> (i & 7) * 8), Operation));
}

// Four guest bytes, twice: what a long of colour looks like in memory.
inline uint64_t ColorWord (uint32_t color)
{
    const uint32_t four = __builtin_bswap32 (color);
    return four | uint64_t (four) << 32;
}

void CombineSpan (uint8_t *dst, const uint8_t *src, size_t length, unsigned operation, uint32_t flipLong)
{
    const uint64_t flip = ColorWord (flipLong);
    switch (operation)
    {
    case 0: Combine<0> (dst, src, length, flip); break;
    case 1: Combine<1> (dst, src, length, flip); break;
    case 2: Combine<2> (dst, src, length, flip); break;
    default: Combine<3> (dst, src, length, flip); break;
    }
}

// The colour loops: where the source, flipped, has a one the pixel takes
// `ones`; where it has a zero it takes `zeros`, or keeps what it had. The two
// colours repeat every four bytes from the start of the span, which is on a
// pixel. The two ranges must not overlap.
template <bool Keep>
void Colorize (uint8_t *dst, const uint8_t *src, size_t length, uint64_t flip,
               uint64_t ones, uint64_t zeros)
{
    size_t i = 0;
    for (; i + 8 <= length; i += 8)
    {
        const uint64_t s = Load64 (src + i) ^ flip;
        const uint64_t rest = Keep ? Load64 (dst + i) : zeros;
        Store64 (dst + i, (s & ones) | (~s & rest));
    }
    for (; i < length; ++i)
    {
        const unsigned at = (i & 7) * 8;
        const uint8_t s = src[i] ^ uint8_t (flip >> at);
        const uint8_t rest = Keep ? dst[i] : uint8_t (zeros >> at);
        dst[i] = uint8_t ((s & uint8_t (ones >> at)) | (~s & rest));
    }
}

void ColorizeSpan (uint8_t *dst, const uint8_t *src, size_t length, const QDBlit &blit)
{
    const uint64_t flip = ColorWord (blit.flip);
    const uint64_t fore = ColorWord (blit.foreColor), back = ColorWord (blit.backColor);
    switch (blit.operation)
    {
    case 0: Colorize<false> (dst, src, length, flip, fore, back); break;
    case 1: Colorize<true> (dst, src, length, flip, fore, 0); break;
    default: Colorize<true> (dst, src, length, flip, back, 0); break;
    }
}

// An expanded pattern row repeats every `mask + 1` bytes, a power of two of
// at least four. Odd modes lay one colour through the pattern used as a mask.
template <unsigned Operation>
void Pattern (uint8_t *dst, const uint8_t *row, unsigned phase, unsigned mask,
              size_t length, uint64_t color)
{
    const auto apply = [color] (uint64_t d, uint64_t s) {
        return Operation & 1 ? (d & ~s) | (color & s) : Transfer (d, s, Operation);
    };
    size_t x = 0;
    for (; x < length && ((phase + x) & 7); ++x)
        dst[x] = uint8_t (apply (dst[x], row[(phase + x) & mask]));
    if (mask == 3)
    {
        uint32_t four;
        memcpy (&four, row, 4);
        const uint64_t eight = four | uint64_t (four) << 32;
        for (; x + 8 <= length; x += 8)
            Store64 (dst + x, apply (Load64 (dst + x), eight));
    }
    else
        for (; x + 8 <= length; x += 8)
            Store64 (dst + x, apply (Load64 (dst + x), Load64 (row + ((phase + x) & mask))));
    for (; x < length; ++x)
        dst[x] = uint8_t (apply (dst[x], row[(phase + x) & mask]));
}

// Scratch for one source row of an overlapping or shifted transfer. Row bytes
// are limited to 0x7fff, and such transfers never run on the second core.
uint8_t s_sourceRow[0x8000 + 16];

// Lays `count` bits of `src`, starting at its bit `from`, into `out` starting
// at bit `to`, which is below 8 and differs from `from` modulo 8. Bits count
// from the most significant of each byte, as QuickDraw packs pixels. What
// `out` holds around them is not defined; nothing is read outside the bytes
// that hold the source bits.
QD_LOOP void ShiftBits (uint8_t *out, const uint8_t *src, unsigned from, unsigned to, unsigned count)
{
    const uint8_t *s = src + from / 8;
    const unsigned phase = from % 8;
    const size_t have = (phase + count + 7) / 8;
    const size_t want = (to + count + 7) / 8;
    size_t j = 0;
    if (phase > to)
    {
        const unsigned left = phase - to, right = 8 - left;
        for (; j + 9 <= have && j + 8 <= want; j += 8)
        {
            const uint64_t word = __builtin_bswap64 (Load64 (s + j));
            Store64 (out + j, __builtin_bswap64 ((word << left) | (s[j + 8] >> right)));
        }
        for (; j < want; ++j)
        {
            const unsigned here = j < have ? s[j] : 0, next = j + 1 < have ? s[j + 1] : 0;
            out[j] = uint8_t ((here << left) | (next >> right));
        }
    }
    else
    {
        const unsigned right = to - phase, left = 8 - right;
        out[j++] = uint8_t (s[0] >> right);
        for (; j + 8 <= have && j + 8 <= want; j += 8)
        {
            const uint64_t word = __builtin_bswap64 (Load64 (s + j));
            Store64 (out + j, __builtin_bswap64 ((word >> right) | (uint64_t (s[j - 1]) << (64 - right))));
        }
        for (; j < want; ++j)
        {
            const unsigned here = j < have ? s[j] : 0;
            out[j] = uint8_t ((s[j - 1] << left) | (here >> right));
        }
    }
}
}

// The two conversions of the scan hooks: an 8-bit destination, one byte a pixel.
static void RunScanSpan (const QDBlit &blit, unsigned y, unsigned left, unsigned right)
{
    auto *dst = blit.destination + size_t (y) * blit.destinationPitch + left;
    const unsigned length = right - left;
    if (blit.sourceDepth == 32)
    {
        const auto *src = blit.source + size_t (y) * blit.sourcePitch + 4 * left;
        const auto *table = blit.inverseTable;
        const unsigned bits = blit.tableResolution;
        const unsigned shift = 8 - bits;
        for (unsigned i = 0; i < length; ++i, src += 4)
        {
            const unsigned index = (unsigned (src[1] >> shift) << (2 * bits)) |
                                   (unsigned (src[2] >> shift) << bits) | (src[3] >> shift);
            dst[i] = table[index];
        }
    }
    else
    {
        // The scales are 1, 2 or 4: shifts, not a division for every pixel.
        const unsigned shiftX = blit.scaleX >> 1, shiftY = blit.scaleY >> 1;
        const auto *src = blit.source + size_t (y >> shiftY) * blit.sourcePitch;
        const auto *mask = blit.mask ? blit.mask + size_t (y) * blit.maskPitch : nullptr;
        if (!mask && !shiftX)
            CopyUp (dst, src + left, length);
        else if (!mask)
            for (unsigned x = left; x < right; ++x)
                dst[x - left] = src[x >> shiftX];
        else
        {
            const unsigned first = blit.maskBit;
            for (unsigned x = left; x < right; ++x)
            {
                const unsigned maskBit = first + x;
                if (mask[maskBit / 8] & (0x80u >> (maskBit % 8)))
                    dst[x - left] = src[x >> shiftX];
            }
        }
    }
}

// One row of a transfer, from pixel `left` to pixel `right` of the rectangle.
// Whole bytes go through the span writers. Below 8 bits a pixel the row can
// start and end inside a byte: those two bytes take the same operation on a
// copy of themselves, and only the bits of the span are kept.
static void RunSpan (const QDBlit &blit, unsigned y, unsigned left, unsigned right)
{
    if (blit.sourceDepth)
    {
        RunScanSpan (blit, y, left, right);
        return;
    }
    const unsigned firstBit = blit.destinationBit + (left << blit.shift);
    const unsigned endBit = blit.destinationBit + (right << blit.shift);
    const unsigned first = firstBit / 8;
    const size_t length = (endBit + 7) / 8 - first;
    auto *dst = blit.destination + size_t (y) * blit.destinationPitch + first;
    const bool whole = !(firstBit % 8) && !(endBit % 8);

    const uint8_t *src = nullptr;
    if (blit.source)
    {
        const unsigned sourceBit = blit.sourceBit + (left << blit.shift);
        const auto *row = blit.source + size_t (y) * blit.sourcePitch;
        if (sourceBit % 8 == firstBit % 8)
        {
            src = row + sourceBit / 8;
            if (whole && !blit.operation && !blit.flip && !blit.colorize)
            {
                if (blit.reverse)
                    CopyDown (dst, src, length);
                else
                    CopyUp (dst, src, length);
                return;
            }
            // Reading the whole source row first is what copying in the
            // right direction amounts to, and it leaves one loop to write.
            if (blit.overlaps)
            {
                CopyUp (s_sourceRow, src, length);
                src = s_sourceRow;
            }
        }
        else
        {
            ShiftBits (s_sourceRow, row, sourceBit, firstBit % 8, endBit - firstBit);
            src = s_sourceRow;
        }
    }

    // `count` bytes of the row, `at` bytes after its first.
    const auto apply = [&blit, src, y, first] (uint8_t *to, size_t at, size_t count) {
        if (src)
        {
            if (blit.colorize)
                ColorizeSpan (to, src + at, count, blit);
            else
                CombineSpan (to, src + at, count, blit.operation, blit.flip);
        }
        else if (blit.patternRows)
        {
            const unsigned mask = blit.patternWidth - 1;
            const auto *row = blit.patternRows +
                ((blit.patternY + y) & (blit.patternHeight - 1)) * blit.patternWidth;
            const unsigned phase = (blit.patternX + first + unsigned (at)) & mask;
            const uint64_t color = ColorWord (blit.patternColor);
            switch (blit.operation)
            {
            case 0: Pattern<0> (to, row, phase, mask, count, color); break;
            case 1: Pattern<1> (to, row, phase, mask, count, color); break;
            case 2: Pattern<2> (to, row, phase, mask, count, color); break;
            default: Pattern<3> (to, row, phase, mask, count, color); break;
            }
        }
        else
            Fill (to, ColorWord (blit.pattern), count);
    };
    if (whole)
    {
        apply (dst, 0, length);
        return;
    }
    const auto edge = [&apply, dst] (size_t at, unsigned mask) {
        uint8_t byte = dst[at];
        apply (&byte, at, 1);
        dst[at] = uint8_t ((dst[at] & ~mask) | (byte & mask));
    };
    const unsigned firstMask = 0xffu >> (firstBit % 8);
    const unsigned lastMask = endBit % 8 ? 0xffu & (0xff00u >> (endBit % 8)) : 0xffu;
    if (length == 1)
    {
        edge (0, firstMask & lastMask);
        return;
    }
    size_t from = 0, to = length;
    if (firstMask != 0xff)
        edge (from++, firstMask);
    if (lastMask != 0xff)
        edge (--to, lastMask);
    if (from < to)
        apply (dst + from, from, to - from);
}

void QDRunBlit (const QDBlit &blit, unsigned first, unsigned end)
{
    for (unsigned row = first; row < end; ++row)
    {
        const unsigned y = blit.reverse ? blit.height - 1 - row : row;
        RunSpan (blit, y, 0, blit.width);
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
                         QDBlit &blit, QDRegion (&regions)[3], int &top, int &left,
                         QDBlitDiagnostic *diagnostic)
{
    const auto reject = [diagnostic] (QDBlitReject reason) {
        if (diagnostic)
            diagnostic->reason = reason;
        return false;
    };
    const auto *args = memory.ram.Map (uint64_t (a[6]) + 10, 30);
    if (!args || a[6] < 0x168)
        return reject (QDBlitReject::Frame);
    // RgnBlt reloads the original rectangles after the dispatch.
    uint32_t addresses[8];
    memcpy (addresses, a, sizeof addresses);
    addresses[2] = Long (args + 0x24 - 10);
    addresses[3] = Long (args + 0x1c - 10);
    addresses[4] = a[6] - 0x168;
    addresses[5] = a[6] - 0xf4;
    if (!QDPrepareBlit (memory, addresses, d, blit, diagnostic))
        return false;
    if (!PrepareRegions (memory, args, ReadRect (memory.ram.Map (a[6] - 0x74, 8)),
                         blit, regions, top, left))
        return reject (QDBlitReject::Region);
    return true;
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
    blit.rowBytes = blit.width = unsigned (clip.Width ());
    blit.shift = 3;
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
    static uint8_t sourceRow[0x8000];
    QDBlit span = blit;
    // Rows run in the order that reads each source row before anything
    // overwrites it. Only a source row that shares bytes with its own
    // destination row, a horizontal move, needs to be put aside first.
    const auto apart = blit.source > blit.destination ? blit.source - blit.destination
                                                      : blit.destination - blit.source;
    const bool saveRow = blit.overlaps && size_t (apart) < blit.rowBytes;
    span.reverse = false;
    span.overlaps = false;
    if (saveRow)
    {
        span.source = sourceRow;
        span.sourcePitch = 0;
    }
    // One bit per pixel of the row, and one more for an edge at its end.
    const size_t edgeBytes = (blit.width / 64 + 1) * sizeof edges[0][0];
    // The bytes a row of the source lies in.
    const size_t sourceBytes = (blit.sourceBit + (blit.width << blit.shift) + 7) / 8;
    for (auto &row : edges)
        memset (row, 0, edgeBytes);
    if (blit.reverse)
        for (unsigned n = 0; n < 3; ++n)
        {
            auto &r = regions[n];
            for (auto *event = r.begin; event != r.end;)
                event = ToggleRegionEdges (edges[n], event, blit.width, left);
            r.event = r.end;
        }
    // The spans of a row are those of the row before it until a region
    // changes: an edge event, or the top or bottom of a rectangular one. A
    // region is mostly rows that repeat, so the list is kept and replayed.
    constexpr unsigned maxSpans = 256;
    static uint16_t spans[maxSpans][2];
    unsigned count = 0, listLeft = 0, listRight = 0;
    bool listed = false;
    for (unsigned line = 0; line < blit.height; ++line)
    {
        const unsigned row = blit.reverse ? blit.height - 1 - line : line;
        const int y = top + int (row);
        bool changed = !listed;
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
                const auto *next = ToggleRegionEdges (edges[n], event, blit.width, left);
                r.event = blit.reverse ? event : next;
                changed = true;
            }
        }
        unsigned clipLeft = 0, clipRight = blit.width;
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
        if (saveRow)
            CopyUp (sourceRow, blit.source + size_t (row) * blit.sourcePitch, sourceBytes);
        if (changed || clipLeft != listLeft || clipRight != listRight)
        {
            // Walk the edges once: into the list, or straight to the pixels
            // for a row with more spans than the list holds.
            listed = true;
            listLeft = clipLeft;
            listRight = clipRight;
            count = 0;
            unsigned start = 0;
            bool active[3];
            for (unsigned n = 0; n < 3; ++n)
                active[n] = !regions[n].end;
            bool drawing = active[0] && active[1] && active[2];
            const auto emit = [&] (unsigned from, unsigned to) {
                if (listed && count < maxSpans)
                {
                    spans[count][0] = uint16_t (from);
                    spans[count++][1] = uint16_t (to);
                    return;
                }
                // Too many: draw what was listed, then the rest as it comes.
                for (unsigned n = 0; listed && n < count; ++n)
                    RunSpan (span, row, spans[n][0], spans[n][1]);
                listed = false;
                RunSpan (span, row, from, to);
            };
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
                        emit (start, x);
                    drawing = visible;
                }
            }
            if (drawing)
                emit (start > clipLeft ? start : clipLeft, clipRight);
            if (!listed)
                continue;   // This row is drawn, and the next one starts afresh.
        }
        for (unsigned n = 0; n < count; ++n)
            RunSpan (span, row, spans[n][0], spans[n][1]);
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
    // D4, D5 and D6 hold four guest bytes each, indexed by the position in
    // the row modulo four. Rotated to the first position, each repeats as is.
    const unsigned turn = (unsigned (left) & 3) * 8;
    const auto repeat = [turn] (uint32_t guest) {
        const uint32_t four = __builtin_bswap32 (guest);
        const uint64_t eight = four | uint64_t (four) << 32;
        return turn ? eight >> turn | eight << (64 - turn) : eight;
    };
    const uint64_t pattern = repeat (d[6]);
    const uint64_t color = operation ? repeat (d[operation == 1 ? 4 : 5]) : pattern;
    const auto apply = [=] (uint64_t pixel, uint64_t gate, unsigned at) {
        if (operation)
            gate &= pattern >> at;
        return operation == 2 ? pixel ^ gate : (pixel & ~gate) | ((color >> at) & gate);
    };
    unsigned x = 0;
    for (; x + 8 <= size; x += 8)
        Store64 (pixels + x, apply (Load64 (pixels + x), Load64 (mask + x), 0));
    for (; x < size; ++x)
        pixels[x] = uint8_t (apply (pixels[x], mask[x], (x & 7) * 8));
    return true;
}
