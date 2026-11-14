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

int main ()
{
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
        ram[0x63f] ^= 1;
        assert (QDPrepareBlit (memory, a, d, blit) == (bytes == 1));
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
    Word (ram + frame - 0x104, 0x108);
    assert (!QDPrepareBlit (memory, a, d, blit, &diagnostic));
    assert (diagnostic.reason == QDBlitReject::Colorize && diagnostic.colorize == 0x108);

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
}
