/*
 *  imgenc.inc - stb_image_write's JPEG and PNG encoders, to memory, included
 *               twice: once per floating-point model (imgenc_soft.c,
 *               imgenc_fpu.c)
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  The JPEG encoder computes its DCT in float. On a 68k compiled for the
 *  68881 the intermediate results are extended and the bytes differ from a
 *  PowerPC's; compiled without, every operation is IEEE single, as on a
 *  PowerPC built with -ffp-contract=off, so the same bytes come out of both.
 *  The soft build makes the JPEG the decoding tests read; the 68881 build is
 *  the one app_jpeg_enc times.
 */

#include <stddef.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#define STBI_WRITE_NO_STDIO
#define STBIW_ASSERT(x) ((void)0)
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image_write.h"

typedef struct { unsigned char *p; long n, cap; } Sink;

static void sink(void *ctx, void *data, int size)
{
	Sink *s = (Sink *)ctx;
	if (s->n + size > s->cap) { s->n = s->cap + 1; return; }	/* too small: reported below */
	memcpy(s->p + s->n, data, size);
	s->n += size;
}

long IMGENC_JPEG(const unsigned char *rgb, int w, int h, int quality, unsigned char *out, long cap)
{
	Sink s = { out, 0, cap };
	if (!stbi_write_jpg_to_func(sink, &s, w, h, 3, rgb, quality) || s.n > cap) return 0;
	return s.n;
}

#ifdef IMGENC_PNG
long IMGENC_PNG(const unsigned char *rgb, int w, int h, unsigned char *out, long cap)
{
	Sink s = { out, 0, cap };
	if (!stbi_write_png_to_func(sink, &s, w, h, 3, rgb, w * 3) || s.n > cap) return 0;
	return s.n;
}
#endif
