/*
 *  apps.c - OkapiaBench's tests of real work: what applications of the time
 *           spent their time on
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Archives (miniz, external/miniz), JPEG and PNG (stb, external/stb), a
 *  text search and word count, a spreadsheet recalculation, IMA ADPCM sound,
 *  and QuickTime's own JPEG decompressor when it is installed. Integer code:
 *  a 68k and a PowerPC agree on every check but app_jpeg_enc's and
 *  app_qt_jpeg's. The data is made once, by appsPrepare, outside the timing.
 */

#include <string.h>
#include <stdlib.h>
#include <Quickdraw.h>

#include "bench.h"

#include "miniz.h"	/* with MINIZ_NO_STDIO and MINIZ_NO_TIME, as CMakeLists.txt builds it */

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_ASSERT(x) ((void)0)
#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"

long jpegEncodeSoft(const unsigned char *rgb, int w, int h, int quality, unsigned char *out, long cap);
long jpegEncodeFpu(const unsigned char *rgb, int w, int h, int quality, unsigned char *out, long cap);
long pngEncode(const unsigned char *rgb, int w, int h, unsigned char *out, long cap);

#define IMG_W 320
#define IMG_H 240
#define TEXT_LEN 262144L

static unsigned char *image, *jpeg, *png, *text, *zip, *work;
static long jpegLen, pngLen, zipLen;
static size_t zipCap;
static int prepared;

/* ---- the data ---- */

/* A picture with what photographs and screenshots have: gradients, edges,
   discs and noise. Integer only, so that every machine makes the same one. */
static void makeImage(void)
{
	long x, y;
	unsigned long seed = 7;
	for (y = 0; y < IMG_H; y++)
		for (x = 0; x < IMG_W; x++) {
			unsigned char *p = image + (y * IMG_W + x) * 3;
			long dx = x - 110, dy = y - 120, ex = x - 230, ey = y - 90;
			int r = (int)(x * 255 / IMG_W), g = (int)(y * 255 / IMG_H), b = (int)((x + y) & 255);
			seed = seed * 1103515245UL + 12345UL;
			if (dx * dx + dy * dy < 70 * 70) { r = 240; g = 200 - (int)(dy & 63); b = 40; }
			if (ex * ex + ey * ey < 45 * 45) { r = 30; g = 60; b = 220 - (int)(ex & 31); }
			if (((x / 16) ^ (y / 16)) & 1 && y > 190) { r /= 2; g /= 2; b /= 2; }
			r += (int)((seed >> 16) & 15) - 8; g += (int)((seed >> 20) & 15) - 8;
			p[0] = r < 0 ? 0 : r > 255 ? 255 : r;
			p[1] = g < 0 ? 0 : g > 255 ? 255 : g;
			p[2] = b;
		}
}

static void makeText(void)
{
	static const char *words[] = { "window ", "the ", "Macintosh ", "of ", "QuickDraw ", "and ", "memory ",
		"handle ", "a ", "resource ", "System ", "folder ", "Finder ", "desktop ", "menu ", "click ",
		"document ", "file ", "disk ", "print ", "font ", "Chooser ", "AppleTalk ", "extension ", "\r" };
	long i = 0;
	unsigned long seed = 4242;
	while (i < TEXT_LEN) {
		const char *w;
		seed = seed * 1103515245UL + 12345UL;
		w = words[(seed >> 16) % 25];
		while (*w && i < TEXT_LEN) text[i++] = *w++;
	}
}

int appsPrepare(void)
{
	mz_zip_archive za;
	void *buf = 0;
	if (prepared) return prepared > 0;
	prepared = -1;
	image = (unsigned char *)NewPtr(IMG_W * IMG_H * 3);
	jpeg = (unsigned char *)NewPtr(131072L);
	png = (unsigned char *)NewPtr(262144L);
	text = (unsigned char *)NewPtr(TEXT_LEN);
	work = (unsigned char *)NewPtr(TEXT_LEN + 4096);
	if (!image || !jpeg || !png || !text || !work) return 0;
	makeImage();
	makeText();
	jpegLen = jpegEncodeSoft(image, IMG_W, IMG_H, 75, jpeg, 131072L);
	pngLen = pngEncode(image, IMG_W, IMG_H, png, 262144L);
	if (!jpegLen || !pngLen) return 0;
	/* an archive as a Macintosh user downloaded one: a text and two pictures */
	memset(&za, 0, sizeof za);
	if (!mz_zip_writer_init_heap(&za, 0, 65536)) return 0;
	if (!mz_zip_writer_add_mem(&za, "ReadMe.txt", text, TEXT_LEN, MZ_DEFAULT_LEVEL)
	    || !mz_zip_writer_add_mem(&za, "Picture.jpg", jpeg, jpegLen, MZ_DEFAULT_LEVEL)
	    || !mz_zip_writer_add_mem(&za, "Picture.png", png, pngLen, MZ_DEFAULT_LEVEL)
	    || !mz_zip_writer_finalize_heap_archive(&za, &buf, &zipCap)) { mz_zip_writer_end(&za); return 0; }
	zip = (unsigned char *)NewPtr(zipCap);
	if (!zip) { mz_zip_writer_end(&za); return 0; }
	memcpy(zip, buf, zipCap); zipLen = zipCap;
	mz_zip_writer_end(&za);
	prepared = 1;
	return 1;
}

/* ---- archives ---- */

unsigned long tDeflate(void)
{
	long iter; unsigned long h = 0; mz_ulong n = 0;
	for (iter = 0; iter < 12L * SCALE; iter++) {
		n = TEXT_LEN + 4096;
		if (mz_compress2(work, &n, text, TEXT_LEN, MZ_DEFAULT_LEVEL) != MZ_OK) return 0;
		h = hashBytes(h ^ (unsigned long)n, work, n > 4096 ? 4096 : n);
	}
	extraNum("bytes=", (unsigned long)n);
	return h;
}

unsigned long tUnzip(void)
{
	long iter; unsigned long h = 2166136261UL;
	for (iter = 0; iter < 60L * SCALE; iter++) {
		mz_zip_archive za; mz_uint k;
		memset(&za, 0, sizeof za);
		if (!mz_zip_reader_init_mem(&za, zip, zipLen, 0)) return 0;
		for (k = 0; k < mz_zip_reader_get_num_files(&za); k++) {
			mz_zip_archive_file_stat st;
			if (!mz_zip_reader_file_stat(&za, k, &st) || st.m_uncomp_size > TEXT_LEN + 4096) break;
			if (!mz_zip_reader_extract_to_mem(&za, k, work, TEXT_LEN + 4096, 0)) break;
			h = hashBytes(h, work + st.m_uncomp_size / 2, 256);
		}
		mz_zip_reader_end(&za);
	}
	return h;
}

/* ---- pictures ---- */

unsigned long tJpegEnc(void)
{
	long iter, n = 0; unsigned long h = 0;
	for (iter = 0; iter < 6L * SCALE; iter++) {
		n = jpegEncodeFpu(image, IMG_W, IMG_H, 75 + (int)(iter & 3) * 5, work, TEXT_LEN);
		h = hashBytes(h, work, n);
	}
	return h;
}

unsigned long tJpegDec(void)
{
	long iter; unsigned long h = 0;
	for (iter = 0; iter < 120L * SCALE; iter++) {
		int w, hh, c;
		unsigned char *p = stbi_load_from_memory(jpeg, (int)jpegLen, &w, &hh, &c, 3);
		if (!p) return 0;
		h = hashBytes(h, p + (long)w * 3 * (hh / 2), (long)w * 3);
		stbi_image_free(p);
	}
	return h;
}

unsigned long tPngDec(void)
{
	long iter; unsigned long h = 0;
	for (iter = 0; iter < 72L * SCALE; iter++) {
		int w, hh, c;
		unsigned char *p = stbi_load_from_memory(png, (int)pngLen, &w, &hh, &c, 3);
		if (!p) return 0;
		h = hashBytes(h, p + (long)w * 3 * (hh / 3), (long)w * 3);
		stbi_image_free(p);
	}
	return h;
}

/* ---- QuickTime's own JPEG decompressor, the one applications called ---- */

#if defined(__ppc__)
int qtAvailable(void) { return 0; }	/* in QuickTimeLib, not InterfaceLib: the 68k build tests it */
unsigned long tQtJpeg(void) { return 0; }
#else
/* The Image Compression Manager's description of compressed data
   (Inside Macintosh: QuickTime), and DecompressImage, selector 8 of its trap. */
typedef struct {
	long idSize; OSType cType; long resvd1; short resvd2, dataRefIndex, version, revisionLevel;
	OSType vendor; unsigned long temporalQuality, spatialQuality;
	short width, height; Fixed hRes, vRes; long dataSize; short frameCount;
	unsigned char name[32]; short depth, clutID;
} ImageDesc;
pascal OSErr DecompressImage(Ptr data, Handle desc, PixMapHandle dst, const Rect *srcRect,
	const Rect *dstRect, short mode, RgnHandle mask) M68K_INLINE(0x7008, 0xAAA3);

int qtAvailable(void)
{
	long v = 0;
	return g_colorQD && sizeof(ImageDesc) == 86 && Gestalt('qtim', &v) == noErr && v != 0;
}

unsigned long tQtJpeg(void)
{
	Handle dh = NewHandleClear(sizeof(ImageDesc));
	GWorldPtr gw = 0; Rect r; long iter; unsigned long h = 0; OSErr err = noErr;
	ImageDesc *d;
	SetRect(&r, 0, 0, IMG_W, IMG_H);
	if (!dh || NewGWorld(&gw, 32, &r, 0L, 0L, 0) != noErr || !gw) { if (dh) DisposeHandle(dh); strcpy(g_extra, "nomem"); return 0; }
	LockPixels(GetGWorldPixMap(gw));
	HLock(dh);
	d = (ImageDesc *)*dh;
	d->idSize = sizeof(ImageDesc); d->cType = 'jpeg'; d->version = 1; d->revisionLevel = 1; d->vendor = 'appl';
	d->spatialQuality = 0x200; d->width = IMG_W; d->height = IMG_H;
	d->hRes = d->vRes = 72L << 16; d->dataSize = jpegLen; d->frameCount = 1;
	memcpy(d->name, "\014Photo - JPEG", 13); d->depth = 24; d->clutID = -1;
	for (iter = 0; iter < 84L * SCALE && err == noErr; iter++)
		err = DecompressImage((Ptr)jpeg, dh, GetGWorldPixMap(gw), &r, &r, srcCopy, 0L);
	if (err == noErr) {
		PixMapHandle pm = GetGWorldPixMap(gw);
		h = hashBytes(0, (unsigned char *)GetPixBaseAddr(pm) + ((**pm).rowBytes & 0x3fff) * (IMG_H / 2), IMG_W * 4);
	} else extraNum("err=", (unsigned long)(long)err);
	DisposeGWorld(gw); DisposeHandle(dh);
	return h;
}
#endif

/* ---- a text: search, replace and count the words ---- */

static long horspool(const unsigned char *s, long n, const char *pat)
{
	long m = strlen(pat), i, count = 0, shift[256];
	for (i = 0; i < 256; i++) shift[i] = m;
	for (i = 0; i < m - 1; i++) shift[(unsigned char)pat[i]] = m - 1 - i;
	for (i = 0; i + m <= n; ) {
		if (s[i + m - 1] == (unsigned char)pat[m - 1] && memcmp(s + i, pat, m - 1) == 0) { count++; i += m; }
		else i += shift[s[i + m - 1]];
	}
	return count;
}

#define HASH_SLOTS 1024
static struct { unsigned long key; long count; } table[HASH_SLOTS];

unsigned long tText2(void)
{
	static const char *finds[] = { "Macintosh", "QuickDraw", "AppleTalk", "extension", "Chooser" };
	long iter, i, o; unsigned long h = 0;
	for (iter = 0; iter < 80L * SCALE; iter++) {
		/* search */
		for (i = 0; i < 5; i++) h += horspool(text, TEXT_LEN, finds[i]);
		/* replace "the" with "a" into another buffer, as a word processor would */
		for (i = 0, o = 0; i < TEXT_LEN; ) {
			if (text[i] == 't' && i + 4 <= TEXT_LEN && memcmp(text + i, "the ", 4) == 0) { work[o++] = 'a'; work[o++] = ' '; i += 4; }
			else work[o++] = text[i++];
		}
		h = hashBytes(h, work + o / 2, 64);
		/* count the words */
		memset(table, 0, sizeof table);
		for (i = 0; i < TEXT_LEN; ) {
			unsigned long k = 2166136261UL;
			while (i < TEXT_LEN && (text[i] == ' ' || text[i] == '\r')) i++;
			while (i < TEXT_LEN && text[i] != ' ' && text[i] != '\r') k = (k ^ text[i++]) * 16777619UL;
			{
				unsigned long slot = k & (HASH_SLOTS - 1);
				while (table[slot].count && table[slot].key != k) slot = (slot + 1) & (HASH_SLOTS - 1);
				table[slot].key = k; table[slot].count++;
			}
		}
		for (i = 0; i < HASH_SLOTS; i++) if (table[i].count) h = h * 31 + (unsigned long)table[i].count;
	}
	return h;
}

/* ---- a spreadsheet: 50 by 50 cells of formulas, in hundredths ---- */

#define ROWS 50
#define COLS 50
static long cells[ROWS][COLS];

unsigned long tSheet(void)
{
	long iter, r, c; unsigned long h = 0;
	for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) cells[r][c] = (r * 31 + c * 17) % 1000 * 100;
	for (iter = 0; iter < 12000L * SCALE; iter++) {
		cells[0][0] = iter * 100;			/* the user types a number */
		for (r = 0; r < ROWS; r++)
			for (c = 0; c < COLS; c++) {
				long v;
				if (r == 0 && c == 0) continue;
				switch ((r + c) & 3) {
				case 0:	/* =A1+B1, the cells above and to the left */
					v = (r ? cells[r - 1][c] : 0) + (c ? cells[r][c - 1] : 0);
					break;
				case 1: {	/* =SUM of the row so far */
					long k; v = 0;
					for (k = 0; k < c; k++) v += cells[r][k];
					break;
				}
				case 2:	/* =above * 1.05, rounded to the cent */
					v = r ? (cells[r - 1][c] * 105 + 50) / 100 : 100;
					break;
				default:	/* =IF(left > above, left - above, above / 2) */
					v = (c && r) ? (cells[r][c - 1] > cells[r - 1][c] ? cells[r][c - 1] - cells[r - 1][c] : cells[r - 1][c] / 2) : 1;
				}
				cells[r][c] = v % 100000000L;
			}
		h = h * 31 + (unsigned long)cells[ROWS - 1][COLS - 1];
	}
	return h;
}

/* ---- IMA ADPCM: ten seconds of 22 kHz sound decoded ---- */

static const short stepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
	5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
	27086, 29794, 32767 };
static const signed char indexTable[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

#define SAMPLES 220500L

static long adpcmDecode(const unsigned char *in, long n, short *out)
{
	long pred = 0, i; int index = 0;
	for (i = 0; i < n; i++) {
		int code = (in[i >> 1] >> ((i & 1) ? 4 : 0)) & 15, step = stepTable[index];
		long diff = step >> 3;
		if (code & 4) diff += step;
		if (code & 2) diff += step >> 1;
		if (code & 1) diff += step >> 2;
		pred += (code & 8) ? -diff : diff;
		if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
		index += indexTable[code];
		if (index < 0) index = 0; else if (index > 88) index = 88;
		out[i] = (short)pred;
	}
	return pred;
}

unsigned long tAdpcm(void)
{
	/* the encoded sound: nibbles drawn from text, which makes a lively signal */
	short *pcm = (short *)NewPtr(SAMPLES * 2);
	long iter; unsigned long h = 0;
	if (!pcm) { strcpy(g_extra, "nomem"); return 0; }
	for (iter = 0; iter < 72L * SCALE; iter++) {
		h += (unsigned long)adpcmDecode(text + (iter & 7) * 1024, SAMPLES, pcm);
		h = hashBytes(h, (unsigned char *)pcm + SAMPLES, 512);
	}
	DisposePtr((Ptr)pcm);
	return h;
}
