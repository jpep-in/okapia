/*
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "quickdraw_blit.h"
#include <assert.h>
#include <initializer_list>
#include <string.h>

static void Word (uint8_t *p, unsigned value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static void Long (uint8_t *p, uint32_t value)
{
    Word (p, value >> 16);
    Word (p + 2, value);
}

static void Rect (uint8_t *p, int top, int left, int bottom, int right)
{
    Word (p, top);
    Word (p + 2, left);
    Word (p + 4, bottom);
    Word (p + 6, right);
}

// The recognition of the System's StretchBits. Apple's code cannot be in
// the repository, so the routines here are made-up bytes and the table is
// built from them; what is checked is how a match is chosen. The real
// table is checked for the shape every row must have.
static void CheckStretchVariants ()
{
    // FNV-1a, 32 bits: the published test vectors.
    assert (QDFingerprint (nullptr, 0) == 0x811c9dc5);
    assert (QDFingerprint (reinterpret_cast<const uint8_t *> ("a"), 1) == 0xe40c292c);
    assert (QDFingerprint (reinterpret_cast<const uint8_t *> ("foobar"), 6) == 0xbf9cf968);

    assert (QDStretchVariantCount == 2);
    for (unsigned n = 0; n < QDStretchVariantCount; ++n)
    {
        const QDStretchVariant &v = QDStretchVariants[n];
        // A return follows a six-byte JSR through the trap table, in the order
        // the routine makes its calls, and the scan loop and the cleanup
        // come after all of them.
        assert (v.name && v.frameBytes >= 0x2c8 && !(v.frameBytes & 3));
        for (unsigned offset : {v.blitReturn, v.regionReturn, v.scaleReturn, v.scanEntry, v.finish})
            assert (!(offset & 1) && offset >= 6);
        assert (v.scaleReturn < v.blitReturn && v.blitReturn < v.regionReturn &&
                v.regionReturn < v.scanEntry && v.scanEntry < v.finish);
        for (unsigned m = 0; m < n; ++m)
        {
            // Two rows of one routine would make the first one win at random.
            assert (QDStretchVariants[m].blitCode != v.blitCode);
            assert (QDStretchVariants[m].regionHandler != v.regionHandler);
        }
    }
    // The routine of 7.1 and later is tried first, and keeps its offsets.
    assert (QDStretchVariants[0].blitReturn == 0x6de && QDStretchVariants[0].regionReturn == 0x712 &&
            QDStretchVariants[0].scaleReturn == 0x680 && QDStretchVariants[0].scanEntry == 0x1084 &&
            QDStretchVariants[0].finish == 0x140a && QDStretchVariants[0].frameBytes == 0x308);
    assert (QDStretchVariants[1].blitReturn == 0x620 && QDStretchVariants[1].regionReturn == 0x654 &&
            QDStretchVariants[1].scaleReturn == 0x5d2 && QDStretchVariants[1].scanEntry == 0xfc4 &&
            QDStretchVariants[1].finish == 0x134a && QDStretchVariants[1].frameBytes == 0x304);

    static uint8_t ram[0x3000];
    const QDMemoryRange range = {0x40000, sizeof ram, ram};
    constexpr uint32_t entry = 0x40100;
    uint8_t *const code = ram + (entry - range.base);
    for (unsigned i = 0; i < 0x2000; ++i)
        code[i] = uint8_t (i * 29 + (i >> 8));
    const auto sum = [&] (unsigned offset, unsigned length) {
        return QDFingerprint (code + offset, length);
    };
    // Two routines laid over the same bytes with different offsets, the
    // second as the real table orders them. Only the second matches the
    // bytes until the first's fingerprints are taken from them too.
    QDStretchVariant table[2] = {
        {"new", 0x308, 0x6de, 0x712, 0x680, 0x1084, 0x140a, 1, 2, 3, 4, 5, 6},
        {"old", 0x304, 0x620, 0x654, 0x5d2, 0xfc4, 0x134a,
         sum (0, 0x624), sum (0, 0x658), sum (0, 0x5d6), sum (0, 0xfc8), sum (0x134a, QD_FINISH_BYTES), 7},
    };
    QDStretchMatch match = QDMatchStretch (range, entry, table, 2);
    assert (match.variant == &table[1] && match.blit && match.region && match.scale && match.scan);

    // The first variant whose code matches anywhere wins, and the second is
    // then not looked at: a call site never comes from another routine.
    table[0].scaleCode = sum (0, 0x684);
    match = QDMatchStretch (range, entry, table, 2);
    assert (match.variant == &table[0] && match.scale && !match.blit && !match.region && !match.scan);

    // stScanLoop needs its cleanup too.
    table[0].scaleCode = 3;
    table[1].finishCode ^= 1;
    match = QDMatchStretch (range, entry, table, 2);
    assert (match.variant == &table[1] && match.blit && !match.scan);
    table[1].finishCode ^= 1;

    // One changed byte between the entry and a return refuses that call
    // and every later one, and leaves the earlier ones.
    code[0x600] ^= 0x80;
    match = QDMatchStretch (range, entry, table, 2);
    assert (match.variant == &table[1] && match.scale && !match.blit && !match.region && !match.scan);
    code[0x600] ^= 0x80;

    // No code at an odd address, none outside the range, and no call site
    // past its end.
    assert (!QDMatchStretch (range, entry + 1, table, 2).variant);
    assert (!QDMatchStretch (range, 0x100, table, 2).variant);
    const QDMemoryRange shorter = {range.base, entry - range.base + 0x600, ram};
    match = QDMatchStretch (shorter, entry, table, 2);
    assert (match.variant == &table[1] && match.scale && !match.blit && !match.region && !match.scan);
    assert (!QDMatchesCode (range, entry + 1, 4, sum (1, 4)));
    assert (QDMatchesCode (range, entry + 2, 4, sum (2, 4)));

    // Neither real routine is a made-up one.
    assert (!QDMatchStretch (range, entry, QDStretchVariants, QDStretchVariantCount).variant);
}

int main ()
{
    CheckStretchVariants ();

    uint8_t ram[4096] = {}, pixels[512] = {}, expected[sizeof pixels];
    const QDMemory memory = {{0, sizeof ram, ram}, {0x10000, sizeof pixels, pixels}};
    constexpr unsigned frame = 0x400;
    uint32_t a[8] = {0, 0, 0x500, 0x510, frame - 0x168, frame - 0xf4, frame, 0};
    uint32_t d[8] = {};
    QDBlit blit;
    for (unsigned shift = 3; shift <= 5; ++shift)
    {
        const unsigned bytes = 1u << (shift - 3);
        d[3] = d[4] = shift;
        for (unsigned pixmap : {a[4], a[5]})
        {
            Long (ram + pixmap, 0x10000);
            Word (ram + pixmap + 4, 0x8040);
            Rect (ram + pixmap + 6, -2, -1, 5, 12);
            Word (ram + pixmap + 32, bytes * 8);
        }
        Word (ram + frame - 0x98, 0);
        for (int direction : {-1, 1})
        {
            for (unsigned i = 0; i < sizeof pixels; ++i)
                pixels[i] = (i * 17) ^ (i >> 7);
            memcpy (expected, pixels, sizeof pixels);
            Rect (ram + a[2], direction, direction, 3 + direction, 8 + direction);
            Rect (ram + a[3], 0, 0, 3, 8);
            Rect (ram + frame - 0x74, 0, 1, 3, 7);
            for (unsigned row = 0; row < 3; ++row)
                memcpy (expected + (row + 2) * 64 + 2 * bytes,
                        pixels + (row + 2 + direction) * 64 + (2 + direction) * bytes, 6 * bytes);
            assert (QDPrepareBlit (memory, a, d, blit) && blit.overlaps);
            QDRunBlit (blit, 0, blit.height);
            assert (memcmp (expected, pixels, sizeof pixels) == 0);
        }

        // The source now lives off screen with a different pitch.
        Long (ram + a[4], 0x800);
        Word (ram + a[4] + 4, 0x8048);
        for (unsigned i = 0; i < 72 * 7; ++i)
            ram[0x800 + i] = (i * 29) ^ 0xae;
        memcpy (expected, pixels, sizeof pixels);
        for (unsigned row = 0; row < 3; ++row)
            memcpy (expected + (row + 2) * 64 + 2 * bytes,
                    ram + 0x800 + (row + 3) * 72 + 3 * bytes, 6 * bytes);
        assert (QDPrepareBlit (memory, a, d, blit) && !blit.overlaps);
        QDRunBlit (blit, 0, blit.height);
        assert (memcmp (expected, pixels, sizeof pixels) == 0);
        Word (ram + a[4] + 32, 1);
        assert (!QDPrepareBlit (memory, a, d, blit));

        Word (ram + frame - 0x98, 8);
        Long (ram + frame - 0x88, 0x600);
        const uint32_t pattern = bytes == 1 ? 0xabababab : bytes == 2 ? 0xabcdabcd : 0x01abcdef;
        for (unsigned row = 0; row < 16; ++row)
            Long (ram + 0x600 + row * 4, pattern);
        Rect (ram + frame - 0x74, 0, 1, 3, 6);
        memcpy (expected, pixels, sizeof pixels);
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned x = 0; x < 5 * bytes; ++x)
                expected[(row + 2) * 64 + 2 * bytes + x] = ram[0x600 + x % bytes];
        assert (QDPrepareBlit (memory, a, d, blit) && !blit.source);
        QDRunBlit (blit, 0, 1);
        QDRunBlit (blit, 1, blit.height);
        assert (memcmp (expected, pixels, sizeof pixels) == 0);
        // A pattern that is not one colour goes row by row, at any depth.
        ram[0x63f] ^= 1;
        assert (QDPrepareBlit (memory, a, d, blit) && blit.patternRows);
        ram[0x63f] ^= 1;
        Long (ram + a[5], 0xfffffffc);
        assert (!QDPrepareBlit (memory, a, d, blit));
        Long (ram + a[5], 0x101ff);
        assert (!QDPrepareBlit (memory, a, d, blit));
    }

    // A rectangular fill intersects a region whose hole opens on the middle row.
    d[3] = d[4] = 3;
    Long (ram + a[5], 0x10000);
    Word (ram + a[5] + 32, 8);
    for (unsigned i = 0; i < 3; ++i)
        Long (ram + frame + 10 + i * 4, 0x700 + i * 4);
    Long (ram + frame + 0x24, a[2]);
    Long (ram + frame + 0x1c, a[3]);
    Long (ram + 0x700, 0x740);
    Long (ram + 0x704, 0x780);
    Long (ram + 0x708, 0x780);
    Word (ram + 0x780, 10);
    Rect (ram + 0x782, 0, 1, 3, 6);
    const unsigned events[] = {
        0, 1, 6, 0x7fff, 1, 2, 4, 0x7fff, 2, 2, 4, 0x7fff,
        3, 1, 6, 0x7fff, 0x7fff
    };
    Word (ram + 0x740, 10 + sizeof events / sizeof *events * 2);
    Rect (ram + 0x742, 0, 1, 3, 6);
    for (unsigned i = 0; i < sizeof events / sizeof *events; ++i)
        Word (ram + 0x74a + 2 * i, events[i]);
    QDRegion regions[3];
    int top, left;
    memset (pixels, 0, sizeof pixels);
    for (unsigned i = 0; i < 64; ++i)
        ram[0x600 + i] = 0xab;
    assert (QDPrepareRegionBlit (memory, a, d, blit, regions, top, left));
    QDRunRegionBlit (blit, regions, top, left);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 1; x < 6; ++x)
            assert (pixels[(y + 2) * 64 + x + 1] == (y == 1 && x >= 2 && x < 4 ? 0 : 0xab));

    Word (ram + frame - 0x98, 2);
    Long (ram + a[4], 0x10000);
    Word (ram + a[4] + 4, 0x8040);
    Word (ram + a[4] + 32, 8);
    Rect (ram + a[2], -1, -1, 2, 7);
    for (unsigned i = 0; i < sizeof pixels; ++i)
        pixels[i] = (i * 17) ^ (i >> 7);
    memcpy (expected, pixels, sizeof pixels);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 1; x < 6; ++x)
            if (y != 1 || x < 2 || x >= 4)
                expected[(y + 2) * 64 + x + 1] ^= pixels[(y + 1) * 64 + x];
    assert (QDPrepareRegionBlit (memory, a, d, blit, regions, top, left) && blit.reverse);
    QDRunRegionBlit (blit, regions, top, left);
    assert (memcmp (expected, pixels, sizeof pixels) == 0);

    Long (ram + a[4], 0x800);
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        Word (ram + frame - 0x98, mode);
        d[7] = mode & 4 ? UINT32_MAX : 0;
        memset (ram + 0x800, 0x4a, 64 * 7);
        memset (pixels, 0x95, sizeof pixels);
        assert (QDPrepareBlit (memory, a, d, blit));
        QDRunBlit (blit, 0, blit.height);
        const unsigned value = (mode & 4) ? 0xb5 : 0x4a;
        const unsigned outputs[] = {value, 0x95 | value, 0x95 ^ value, 0x95 & ~value};
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 1; x < 6; ++x)
                assert (pixels[(y + 2) * 64 + x + 1] == outputs[mode & 3]);
    }
    d[7] = 0;

    // The colour loops: BGnotWhite set, a foreground and a background. Copy
    // lays both; OR lays the foreground and BIC the background where the
    // source, inverted or not, has a one. XOR never looks at the word.
    Word (ram + frame - 0x104, 1);
    Long (ram + frame - 0xa0, 0xc3c3c3c3);
    Long (ram + frame - 0xa4, 0x18181818);
    for (unsigned mode = 0; mode < 8; ++mode)
    {
        Word (ram + frame - 0x98, mode);
        d[7] = mode & 4 ? UINT32_MAX : 0;
        memset (ram + 0x800, 0x4a, 64 * 7);
        memset (pixels, 0x95, sizeof pixels);
        assert (QDPrepareBlit (memory, a, d, blit) && blit.colorize == ((mode & 3) != 2));
        QDRunBlit (blit, 0, blit.height);
        const unsigned value = (mode & 4) ? 0xb5 : 0x4a;
        const unsigned outputs[] = {(value & 0xc3) | (~value & 0x18), (0x95 & ~value) | (value & 0xc3),
                                    0x95 ^ value, (0x95 & ~value) | (value & 0x18)};
        for (unsigned y = 0; y < 3; ++y)
            for (unsigned x = 1; x < 6; ++x)
                assert (pixels[(y + 2) * 64 + x + 1] == outputs[mode & 3]);
    }
    d[7] = 0;
    // Black on white through those loops is the plain transfer, and a colour
    // that is not one pixel repeated is left to the guest.
    Word (ram + frame - 0x98, 0);
    Long (ram + frame - 0xa0, 0xffffffff);
    Long (ram + frame - 0xa4, 0);
    assert (QDPrepareBlit (memory, a, d, blit) && !blit.colorize);
    QDBlitDiagnostic colors = {};
    // Alpha drawing goes through loops the System has replaced: at no depth
    // is it this path's business.
    ram[frame - 0x92] = 0xff;
    assert (!QDPrepareBlit (memory, a, d, blit, &colors) && colors.reason == QDBlitReject::Alpha);
    ram[frame - 0x92] = 0;
    Long (ram + frame - 0xa0, 0x12345678);
    assert (!QDPrepareBlit (memory, a, d, blit, &colors));
    assert (colors.reason == QDBlitReject::Colorize && colors.colorize == 1);
    Long (ram + frame - 0xa0, 0);

    // A pattern mode does not look at the word either: it stays set here.
    Word (ram + frame - 0x98, 10);
    for (unsigned i = 0; i < 64; ++i)
        ram[0x600 + i] = uint8_t (i ^ 0x55);
    memset (pixels, 0x30, sizeof pixels);
    assert (QDPrepareBlit (memory, a, d, blit));
    QDRunBlit (blit, 0, blit.height);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 1; x < 6; ++x)
            assert (pixels[(y + 2) * 64 + x + 1] == (0x30 ^ ram[0x600 + y * 4 + (x + 1) % 4]));

    a[1] = 0x10000;
    a[2] = 0x600;
    d[1] = 0;
    d[2] = 9;
    d[6] = 0x12345678;
    Rect (ram + a[3], 0, 1, 1, 6);
    for (unsigned x = 0; x < 10; ++x)
        ram[0x600 + x] = x & 1 ? 0xff : 0x0f;
    memset (pixels, 0xa0, sizeof pixels);
    assert (QDSlab8 (memory, a, d, 0, true));
    const uint8_t want[] = {0xa0, 0x34, 0xa6, 0x78, 0xa2, 0x34, 0xa0};
    assert (memcmp (pixels, want, sizeof want) == 0);

    QDBlitDiagnostic diagnostic = {};
    assert (!QDPrepareBlit (memory, a, d, blit, &diagnostic));
    assert (diagnostic.reason == QDBlitReject::Rect && diagnostic.mode == 10);
    Word (ram + frame - 0x98, 50);
    assert (!QDPrepareBlit (memory, a, d, blit, &diagnostic));
    assert (diagnostic.reason == QDBlitReject::Mode && diagnostic.mode == 50);
    Word (ram + frame - 0x98, 0);

    // ScaleBlt indexes the current device's RGB inverse table. Clipping
    // shifts the source origin, and the second row has a region hole.
    Word (ram + frame - 0x104, 0);
    a[2] = 0x500;
    Long (ram + frame + 0x24, a[2]);
    auto *src = ram + a[4];
    auto *dst = ram + a[5];
    Long (src, 0x800);
    Word (src + 30, 16);
    Word (src + 32, 32);
    Word (src + 34, 3);
    Word (src + 36, 8);
    Rect (ram + a[2], 0, 0, 3, 8);
    Rect (ram + a[3], 0, 0, 3, 8);
    Rect (ram + frame - 0x74, 0, 1, 3, 6);
    Long (dst + 42, 0xc00);
    Long (ram + 0xcc8, 0xc04);
    Long (ram + 0xc00, 0xc10);
    Long (ram + 0xc04, 0xc20);
    Long (ram + 0xc26, 0xc40);
    Long (ram + 0xc36, 0xc44);
    Long (ram + 0xc40, 0xd00);
    Long (ram + 0xc44, a[5]);
    Long (ram + 0xc10, 0xdad);
    Long (ram + 0xd00, 0xdad);
    Word (ram + 0xd04, 3);
    const unsigned rgb = (5 << 6) | (2 << 3) | 7;
    ram[0xd06 + rgb] = 0x70;
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 8; ++x)
            Long (ram + 0x800 + (y + 2) * 64 + (x + 1) * 4, 0xffa040e0);
    memset (pixels, 0, sizeof pixels);
    assert (QDPrepareScanBlit (memory, frame, true, false, blit, regions, top, left));
    QDRunRegionBlit (blit, regions, top, left);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 1; x < 6; ++x)
            assert (pixels[(y + 2) * 64 + x + 1] ==
                    (y == 1 && x >= 2 && x < 4 ? 0 : 0x70));
    Word (ram + 0xf08, 0x8000);  // Gray table.
    assert (!QDPrepareScanBlit (memory, frame, true, false, blit, regions, top, left));
    Word (ram + 0xf08, 0);
    Long (ram + 0xd00, 0xbaad);  // Stale table.
    assert (!QDPrepareScanBlit (memory, frame, true, false, blit, regions, top, left));

    // General scan with a 1-bit stencil and a matching indexed palette.
    Word (src + 30, 0);
    Word (src + 32, 8);
    memset (ram + 0x800, 0x1b, 64 * 7);
    Long (src + 42, 0xc00);
    Long (dst + 42, 0xc00);
    auto *mask = ram + frame - 0x1b8;
    Long (ram + frame + 0x2c, 0xa80);
    Long (ram + frame + 0x20, 0x520);
    Rect (ram + 0x520, 0, 0, 3, 8);
    Long (mask, 0xa40);
    Word (mask + 4, 2);
    Word (mask + 32, 1);
    Rect (mask + 6, 0, 0, 3, 8);
    memset (ram + 0xa40, 0x55, 6);
    memset (pixels, 0, sizeof pixels);
    assert (QDPrepareScanBlit (memory, frame, false, false, blit, regions, top, left));
    QDRunRegionBlit (blit, regions, top, left);
    for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 1; x < 6; ++x)
            assert (pixels[(y + 2) * 64 + x + 1] ==
                    ((x & 1) && (y != 1 || x < 2 || x >= 4) ? 0x1b : 0));

    // Exact integer expansion with an unchanged palette.
    Long (ram + frame + 0x2c, 0);
    Rect (ram + a[2], 0, 0, 1, 2);
    Rect (ram + a[3], 0, 0, 2, 4);
    Rect (ram + frame - 0x74, 0, 0, 2, 4);
    Rect (ram + 0x782, 0, 0, 2, 4);
    Long (ram + 0x700, 0x780);
    ram[0x881] = 0x21;
    ram[0x882] = 0x47;
    memset (pixels, 0, sizeof pixels);
    assert (QDPrepareScanBlit (memory, frame, false, false, blit, regions, top, left));
    QDRunRegionBlit (blit, regions, top, left);
    const uint8_t expanded[] = {0, 0x21, 0x21, 0x47, 0x47, 0};
    assert (memcmp (pixels + 2 * 64, expanded, sizeof expanded) == 0);
    assert (memcmp (pixels + 3 * 64, expanded, sizeof expanded) == 0);

    // The span writers move eight bytes at a time. Each is compared with its
    // definition, one byte after the other, over lengths and alignments that
    // start, cross and end inside a word, and with the source overlapping the
    // destination from both sides.
    static uint8_t surface[4096], model[4096], patternRows[16 * 16];
    uint32_t seed = 20261115;
    const auto next = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    const auto transfer = [] (unsigned dst, unsigned src, unsigned operation) -> uint8_t {
        return operation == 0 ? src : operation == 1 ? dst | src :
               operation == 2 ? dst ^ src : dst & ~src;
    };
    const auto colorize = [] (unsigned dst, unsigned src, unsigned operation,
                              unsigned fore, unsigned back) -> uint8_t {
        return operation == 0 ? (src & fore) | (~src & back) :
               operation == 1 ? (dst & ~src) | (src & fore) : (dst & ~src) | (src & back);
    };
    for (unsigned round = 0; round < 80000; ++round)
    {
        for (auto &byte : surface)
            byte = next ();
        for (auto &byte : patternRows)
            byte = next ();
        memcpy (model, surface, sizeof model);
        QDBlit b = {};
        b.pixelBytes = 1;
        b.shift = 3;
        b.destinationPitch = b.sourcePitch = 128;
        b.height = 1 + next () % 5;
        b.rowBytes = b.width = 1 + next () % 70;
        b.operation = next () % 4;
        const unsigned at = 1536 + next () % 40;
        b.destination = surface + at;
        const size_t span = (b.height - 1) * 128 + b.rowBytes;
        if (round % 4 == 0)
        {
            const unsigned from = at + next () % 600 - 300;
            b.source = surface + from;
            b.flip = next () & 1 ? UINT32_MAX : 0;
            b.colorize = b.operation != 2 && (next () & 1);
            b.foreColor = (next () & 0xff) * 0x01010101u;
            b.backColor = (next () & 0xff) * 0x01010101u;
            b.overlaps = from < at + span && at < from + span;
            b.reverse = b.overlaps && at > from;
            for (unsigned line = 0; line < b.height; ++line)
            {
                const unsigned y = b.reverse ? b.height - 1 - line : line;
                for (unsigned i = 0; i < b.rowBytes; ++i)
                {
                    const unsigned x = b.reverse ? b.rowBytes - i - 1 : i;
                    auto &dst = model[at + y * 128 + x];
                    const unsigned src = model[from + y * 128 + x] ^ (b.flip & 0xff);
                    dst = b.colorize ? colorize (dst, src, b.operation, b.foreColor & 0xff, b.backColor & 0xff)
                                     : transfer (dst, src, b.operation);
                }
            }
        }
        else if (round % 4 == 1)
        {
            b.patternRows = patternRows;
            b.patternWidth = 4u << next () % 3;
            b.patternHeight = 1u << next () % (b.patternWidth == 16 ? 5 : 3);
            b.patternX = next () % 64;
            b.patternY = next () % 64;
            b.patternColor = (next () & 0xff) * 0x01010101u;
            for (unsigned y = 0; y < b.height; ++y)
                for (unsigned x = 0; x < b.rowBytes; ++x)
                {
                    const unsigned src = patternRows[((b.patternY + y) & (b.patternHeight - 1)) * b.patternWidth +
                                                     ((b.patternX + x) & (b.patternWidth - 1))];
                    auto &dst = model[at + y * 128 + x];
                    dst = b.operation & 1 ? (dst & ~src) | (b.patternColor & 0xff & src)
                                          : transfer (dst, src, b.operation);
                }
        }
        else if (round % 4 == 2)
        {
            // Deeper pixels through the colour loops: the two colours are
            // longs, laid from the first byte of the row.
            b.pixelBytes = round % 8 == 2 ? 2 : 4;
            b.shift = b.pixelBytes == 2 ? 4 : 5;
            b.width = 1 + next () % 30;
            b.rowBytes = b.width * b.pixelBytes;
            b.operation = next () % 3 == 2 ? 3 : next () & 1;
            b.source = surface + at + 1024;
            // An inversion that spares the alpha bits, as 32-bit QuickDraw passes.
            b.flip = b.pixelBytes == 4 ? (next () & 1 ? 0x00ffffffu : UINT32_MAX) : next () & 1 ? UINT32_MAX : 0;
            b.colorize = next () & 1;
            b.foreColor = next () << 8 | (next () & 0xff);
            b.backColor = next () << 8 | (next () & 0xff);
            if (b.pixelBytes == 2)
            {
                b.foreColor = (b.foreColor & 0xffff) * 0x10001u;
                b.backColor = (b.backColor & 0xffff) * 0x10001u;
            }
            for (unsigned y = 0; y < b.height; ++y)
                for (unsigned x = 0; x < b.rowBytes; ++x)
                {
                    const unsigned shift = 24 - (x & 3) * 8;
                    auto &dst = model[at + y * 128 + x];
                    const unsigned src = model[at + 1024 + y * 128 + x] ^ uint8_t (b.flip >> shift);
                    dst = b.colorize ? colorize (dst, src, b.operation,
                                                 uint8_t (b.foreColor >> shift), uint8_t (b.backColor >> shift))
                                     : transfer (dst, src, b.operation);
                }
        }
        else
        {
            b.operation = 0;
            b.pixelBytes = round % 8 == 3 ? 2 : 4;
            b.shift = b.pixelBytes == 2 ? 4 : 5;
            b.width = 1 + next () % 30;
            b.rowBytes = b.width * b.pixelBytes;
            b.pattern = next () << 8 | (next () & 0xff);
            if (b.pixelBytes == 2)
                b.pattern = (b.pattern & 0xffff) * 0x10001u;
            for (unsigned y = 0; y < b.height; ++y)
                for (unsigned x = 0; x < b.rowBytes; ++x)
                    model[at + y * 128 + x] = b.pattern >> (24 - (x & 3) * 8);
        }
        QDRunBlit (b, 0, b.height);
        assert (memcmp (surface, model, sizeof surface) == 0);
    }

    // Below 8 bits a pixel, a row starts and ends anywhere in a byte and the
    // source need not start at the same bit as the destination. The model
    // works one bit at a time, from the surface as it was before the call:
    // that is what a transfer means when the two rectangles overlap.
    const auto bitAt = [] (const uint8_t *bytes, size_t bit) -> unsigned {
        return (bytes[bit / 8] >> (7 - bit % 8)) & 1;
    };
    static uint8_t before[sizeof surface];
    for (unsigned round = 0; round < 60000; ++round)
    {
        for (auto &byte : surface)
            byte = next ();
        for (auto &byte : patternRows)
            byte = next ();
        memcpy (model, surface, sizeof model);
        memcpy (before, surface, sizeof before);
        QDBlit b = {};
        b.pixelBytes = 1;
        b.shift = next () % 4;
        b.destinationPitch = b.sourcePitch = 64;
        b.height = 1 + next () % 5;
        b.width = 1 + next () % (300 >> b.shift);
        b.operation = next () % 4;
        const size_t to = (1536 + next () % 40) * 8 + (next () % (8 >> b.shift) << b.shift);
        b.destination = surface + to / 8;
        b.destinationBit = to % 8;
        b.rowBytes = (b.destinationBit + (b.width << b.shift) + 7) / 8;
        const size_t rowBits = size_t (b.width) << b.shift;
        size_t from = 0;
        const unsigned kind = round % 3;
        if (kind == 0)
        {
            from = to + (size_t (next () % 2400) << b.shift) - (size_t (1200) << b.shift);
            b.source = surface + from / 8;
            b.sourceBit = from % 8;
            b.serial = b.sourceBit != b.destinationBit;
            b.flip = next () & 1 ? UINT32_MAX : 0;
            b.colorize = b.operation != 2 && (next () & 1);
            b.foreColor = (next () & 0xff) * 0x01010101u;
            b.backColor = (next () & 0xff) * 0x01010101u;
            const size_t span = (b.height - 1) * 64 + b.rowBytes;
            const size_t sourceSpan = (b.height - 1) * 64 + (b.sourceBit + rowBits + 7) / 8;
            b.overlaps = from / 8 < to / 8 + span && to / 8 < from / 8 + sourceSpan;
            b.reverse = b.overlaps && to / 8 > from / 8;
        }
        else if (kind == 1)
        {
            b.patternRows = patternRows;
            b.patternWidth = 4u << next () % 3;
            b.patternHeight = 1u << next () % (b.patternWidth == 16 ? 5 : 3);
            b.patternX = next () % 64;
            b.patternY = next () % 64;
            b.patternColor = (next () & 0xff) * 0x01010101u;
        }
        else
        {
            b.operation = 0;
            b.pattern = (next () & 0xff) * 0x01010101u;
        }
        // Half the rounds go through the region runner, clipped to a rectangle
        // of their own: its spans start anywhere in the row.
        const bool clipped = next () & 1;
        QDRegion clip[3] = {{nullptr, nullptr, nullptr, 0, 0, int (b.height), int (b.width)},
                            {nullptr, nullptr, nullptr, 0, 0, int (b.height), int (b.width)},
                            {nullptr, nullptr, nullptr, 0, 0, int (b.height), int (b.width)}};
        if (clipped)
        {
            auto &r = clip[next () % 3];
            r.top = int (next () % b.height);
            r.bottom = r.top + 1 + int (next () % (b.height - r.top));
            r.left = int (next () % b.width);
            r.right = r.left + 1 + int (next () % (b.width - r.left));
        }
        for (unsigned y = 0; y < b.height; ++y)
            for (size_t i = 0; i < rowBits; ++i)
            {
                const int column = int (i >> b.shift);
                bool visible = true;
                for (const auto &r : clip)
                    visible = visible && int (y) >= r.top && int (y) < r.bottom && column >= r.left && column < r.right;
                if (!visible)
                    continue;
                const size_t at = to + y * 512 + i;
                const unsigned inByte = 7 - at % 8;
                const unsigned dst = bitAt (before, at);
                unsigned value;
                if (kind == 0)
                {
                    const unsigned src = bitAt (before, from + y * 512 + i) ^ (b.flip & 1);
                    value = b.colorize ? colorize (dst, src, b.operation, (b.foreColor >> inByte) & 1,
                                                   (b.backColor >> inByte) & 1) & 1
                                       : transfer (dst, src, b.operation) & 1;
                }
                else if (kind == 1)
                {
                    const size_t patternColumn = b.patternX + at / 8 - to / 8;
                    const unsigned src = (patternRows[((b.patternY + y) & (b.patternHeight - 1)) * b.patternWidth +
                                                      (patternColumn & (b.patternWidth - 1))] >> inByte) & 1;
                    value = b.operation & 1 ? (dst & ~src) | ((b.patternColor >> inByte) & src)
                                            : transfer (dst, src, b.operation) & 1;
                }
                else
                    value = (b.pattern >> inByte) & 1;
                model[at / 8] = uint8_t ((model[at / 8] & ~(1u << inByte)) | ((value & 1) << inByte));
            }
        if (clipped)
            QDRunRegionBlit (b, clip, 0, 0);
        else
            QDRunBlit (b, 0, b.height);
        assert (memcmp (surface, model, sizeof surface) == 0);
    }

    // DrawSlab's four modes, with the pattern and both colours rotated to
    // every starting column.
    a[1] = 0x10000;
    a[2] = 0x600;
    Rect (ram + a[3], 0, 0, 1, 400);
    for (unsigned round = 0; round < 40000; ++round)
    {
        for (unsigned i = 0; i < sizeof pixels; ++i)
        {
            pixels[i] = next ();
            ram[0x600 + i] = next () & 1 ? next () : next () & 1 ? 0xff : 0;
        }
        memcpy (expected, pixels, sizeof pixels);
        const unsigned start = next () % 300, size = 1 + next () % 90, operation = round % 4;
        d[1] = start;
        d[2] = start + size;
        for (unsigned n : {4, 5, 6})
            d[n] = next () << 8 | (next () & 0xff);
        for (unsigned x = start; x < start + size; ++x)
        {
            const unsigned shift = 24 - (x & 3) * 8;
            const unsigned pattern = uint8_t (d[6] >> shift);
            unsigned gate = ram[0x600 + x];
            if (operation)
                gate &= pattern;
            const unsigned color = operation ? uint8_t (d[operation == 1 ? 4 : 5] >> shift) : pattern;
            expected[x] = operation == 2 ? expected[x] ^ gate : (expected[x] & ~gate) | (color & gate);
        }
        assert (QDSlab8 (memory, a, d, operation, true));
        assert (memcmp (expected, pixels, sizeof pixels) == 0);
    }
}
