/*
 *  macbench.c - a self-running benchmark for a 68k Macintosh
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  MIT rather than Okapia's GPL, so that anyone can take it — Basilisk II,
 *  another emulator, a period machine's owner. The text is in LICENSE, here.
 *
 *  Runs a fixed list of tests, each a fixed amount of work, times each one
 *  with TickCount and writes "name ticks check" lines to the file "MacBench
 *  Results" at the root of the volume named "Unix" (the folder an emulator
 *  shares with its host), or next to the application when there is none.
 *  `check` is a hash of what the test computed or drew: two machines that
 *  disagree on it did not do the same work.
 *
 *  With an FPU (Gestalt), five floating-point tests follow (fpu.c).
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Memory.h>
#include <Files.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <TextUtils.h>
#include <Gestalt.h>	/* gestaltFPUType: 0 when there is none */

#include "fpu.h"

#ifdef DEPTH	/* the Palette Manager's depth calls, which these headers leave out */
pascal short HasDepth(GDHandle device, short depth, short whichFlags, short flags) M68K_INLINE(0x303C, 0x0A14, 0xAAA2);
pascal OSErr SetDepth(GDHandle device, short depth, short whichFlags, short flags) M68K_INLINE(0x303C, 0x0A13, 0xAAA2);
#endif
#include <string.h>

#ifndef SCALE
#define SCALE 1		/* multiplies every iteration count */
#endif

static WindowPtr win;
static Rect winRect = { 40, 10, 440, 610 };
static char out[4096];
static long outLen;

static unsigned long rngState;
static unsigned long rnd(void)
{
	rngState = rngState * 1103515245UL + 12345UL;
	return rngState >> 8;
}

static unsigned long hashBytes(unsigned long h, const unsigned char *p, long n)
{
	while (n--) h = (h ^ *p++) * 16777619UL;
	return h;
}

/* What is on screen inside the window, read straight from the frame buffer.
   Below eight bits a pixel is part of a byte: the bytes the window's columns
   fall in are hashed whole, neighbours included. */
static unsigned long hashWindow(void)
{
	PixMapHandle pm = (**GetMainDevice()).gdPMap;
	long rowBytes = (**pm).rowBytes & 0x3fff;
	long bits = (**pm).pixelSize;
	long first = winRect.left * bits / 8, count = (winRect.right * bits + 7) / 8 - first;
	unsigned char *base = (unsigned char *)(**pm).baseAddr;
	unsigned long h = 2166136261UL;
	short y;
	for (y = winRect.top; y < winRect.bottom; y += 4)
		h = hashBytes(h, base + y * rowBytes + first, count);
	return h;
}

#ifdef STRETCH_DEBUG	/* the same, for one rectangle of the window: which part of a drawing differs */
static unsigned long hashPart(short top, short left, short bottom, short right)
{
	PixMapHandle pm = (**GetMainDevice()).gdPMap;
	long rowBytes = (**pm).rowBytes & 0x3fff;
	unsigned char *base = (unsigned char *)(**pm).baseAddr;
	unsigned long h = 2166136261UL;
	short y;
	for (y = winRect.top + top; y < winRect.top + bottom; y++)
		h = hashBytes(h, base + y * rowBytes + winRect.left + left, right - left);
	return h;
}
static void putPart(const char *name, unsigned long h);
#endif

static void put(const char *s) { while (*s && outLen < (long)sizeof out - 1) out[outLen++] = *s++; }
static void putNum(unsigned long v)
{
	char b[12]; int i = 11;
	b[i] = 0;
	do { b[--i] = '0' + v % 10; v /= 10; } while (v);
	put(b + i);
}
static void putHex(unsigned long v)
{
	static const char d[] = "0123456789abcdef";
	char b[9]; int i;
	for (i = 7; i >= 0; i--) { b[i] = d[v & 15]; v >>= 4; }
	b[8] = 0;
	put(b);
}

typedef unsigned long (*TestProc)(void);

#ifdef STRETCH_DEBUG
static void putPart(const char *name, unsigned long h) { put(name); put(" 0 "); putHex(h); put("\r"); }
#endif

static void run(const char *name, TestProc proc)
{
	unsigned long t, check;
	Str255 title;
	int n = strlen(name);
#ifdef REPEAT
	int pass;
#endif
#ifdef ONLY
	if (strcmp(name, ONLY) != 0) return;	/* a build that runs one test, to look at it alone */
#endif
#ifdef REPEAT	/* the same test again and again: is its check the same every time? */
	for (pass = 0; pass < REPEAT; pass++) {
#endif
	title[0] = n; memcpy(title + 1, name, n);
	SetWTitle(win, title);
	SetPort(win);
	PenNormal(); ForeColor(blackColor); BackColor(whiteColor);
	EraseRect(&win->portRect);
	rngState = 12345;
	t = TickCount();
	while (TickCount() == t) ;		/* start on a tick edge */
	t = TickCount();
	check = proc();
	t = TickCount() - t;
	put(name); put(" "); putNum(t); put(" "); putHex(check); put("\r");
#ifdef REPEAT
	}
#endif
}

/* ---- the processor alone ---- */

static unsigned long tSieve(void)
{
	static unsigned char flags[8192];
	long iter, i, k, count = 0;
	for (iter = 0; iter < 6000L * SCALE; iter++) {
		count = 0;
		for (i = 0; i < 8192; i++) flags[i] = 1;
		for (i = 2; i < 8192; i++)
			if (flags[i]) { for (k = i + i; k < 8192; k += i) flags[k] = 0; count++; }
	}
	return count;
}

static long sortBuf[20000];
static void quick(long *a, long lo, long hi)
{
	while (lo < hi) {
		long p = a[(lo + hi) >> 1], i = lo, j = hi, t;
		while (i <= j) {
			while (a[i] < p) i++;
			while (a[j] > p) j--;
			if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
		}
		if (j - lo < hi - i) { quick(a, lo, j); lo = i; } else { quick(a, i, hi); hi = j; }
	}
}
static unsigned long tSort(void)
{
	long iter, i; unsigned long h = 0;
	for (iter = 0; iter < 150L * SCALE; iter++) {
		for (i = 0; i < 20000; i++) sortBuf[i] = rnd();
		quick(sortBuf, 0, 19999);
		h = hashBytes(h, (unsigned char *)sortBuf, 256);
	}
	return h;
}

static unsigned char *bufA, *bufB, *bufC;
#define BUF 65536L

static unsigned long tCopy(void)
{
	long iter, i; unsigned long *s, *d;
	for (iter = 0; iter < 12000L * SCALE; iter++) {
		s = (unsigned long *)bufA; d = (unsigned long *)bufB;
		for (i = 0; i < BUF / 4; i++) *d++ = *s++;
	}
	return hashBytes(2166136261UL, bufB, BUF);
}

static unsigned long tCrc(void)
{
	static unsigned long table[256];
	unsigned long c = 0; long i, k, iter;
	for (i = 0; i < 256; i++) {
		c = i;
		for (k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
		table[i] = c;
	}
	for (iter = 0; iter < 1250L * SCALE; iter++) {
		c = 0xffffffffUL;
		for (i = 0; i < BUF; i++) c = table[(c ^ bufA[i]) & 255] ^ (c >> 8);
	}
	return ~c;
}

/* A small LZ77: literal runs and (offset, length) copies, as archive tools of the time did */
static long lzPack(const unsigned char *src, long n, unsigned char *dst)
{
	static short head[4096];
	long i = 0, o = 0, k;
	for (k = 0; k < 4096; k++) head[k] = -1;
	while (i < n) {
		long best = 0, pos = 0;
		if (i + 3 <= n) {
			unsigned h = ((src[i] << 8) ^ (src[i + 1] << 4) ^ src[i + 2]) & 4095;
			long cand = head[h];
			if (cand >= 0) {
				cand += (i & ~32767L);
				if (cand >= i) cand -= 32768L;
				if (cand >= 0 && i - cand < 4096) {
					while (best < 18 && i + best < n && src[cand + best] == src[i + best]) best++;
					pos = i - cand;
				}
			}
			head[h] = (short)(i & 32767);
		}
		if (best >= 3) { dst[o++] = 0x80 | (best - 3) << 3 | (pos >> 8); dst[o++] = pos & 255; i += best; }
		else {
			long run = 1;
			while (run < 127 && i + run < n) run++;		/* greedy: literals to the next chance */
			if (run > 8) run = 8;
			dst[o++] = run; memcpy(dst + o, src + i, run); o += run; i += run;
		}
	}
	return o;
}
static long lzUnpack(const unsigned char *src, long n, unsigned char *dst)
{
	long i = 0, o = 0;
	while (i < n) {
		unsigned c = src[i++];
		if (c & 0x80) {
			long len = ((c >> 3) & 15) + 3, pos = ((c & 7) << 8) | src[i++];
			const unsigned char *p = dst + o - pos;
			while (len--) dst[o++] = *p++;
		} else {
			while (c--) dst[o++] = src[i++];
		}
	}
	return o;
}
static long packedLen;
static unsigned long tUnpack(void)
{
	long iter, n = 0;
	for (iter = 0; iter < 2000L * SCALE; iter++)
		n = lzUnpack(bufC, packedLen, bufB);
	return hashBytes(n, bufB, n);
}

/* ---- the Toolbox outside QuickDraw ---- */

static unsigned long tPackBits(void)
{
	long iter; unsigned long h = 0;
	for (iter = 0; iter < 3600L * SCALE; iter++) {
		Ptr s = (Ptr)bufA, d = (Ptr)bufB, e;
		long left = 32000;
		while (left > 0) { PackBits(&s, &d, 100); left -= 100; }
		e = d; s = (Ptr)bufB; d = (Ptr)bufC + BUF;
		while (s < e) UnpackBits(&s, &d, 100);
		h = hashBytes(h, bufB, 512);
	}
	return h;
}

static unsigned long tMemory(void)
{
	Handle hs[64]; long iter, i; unsigned long sum = 0;
	for (iter = 0; iter < 8000L * SCALE; iter++) {
		for (i = 0; i < 64; i++) { hs[i] = NewHandle(64 + (rnd() & 1023)); if (hs[i]) sum += GetHandleSize(hs[i]); }
		for (i = 0; i < 64; i += 2) if (hs[i]) { DisposeHandle(hs[i]); hs[i] = 0; }
		for (i = 1; i < 64; i += 2) if (hs[i]) SetHandleSize(hs[i], 2048);
		for (i = 1; i < 64; i += 2) if (hs[i]) DisposeHandle(hs[i]);
	}
	return sum;
}

static unsigned long tStrings(void)
{
	long iter; Str255 s; long v; unsigned long h = 0;
	for (iter = 0; iter < 2000000L * SCALE; iter++) {
		NumToString((long)rnd(), s);
		StringToNum(s, &v);
		h += v + s[0];
	}
	return h;
}

/* ---- QuickDraw ---- */

static void randRect(Rect *r, short maxW, short maxH)
{
	short w = 4 + rnd() % maxW, h = 4 + rnd() % maxH;
	r->left = rnd() % (600 - w); r->top = rnd() % (400 - h);
	r->right = r->left + w; r->bottom = r->top + h;
}

static unsigned long tRects(void)
{
	long i; Rect r; static const long colors[] = { blackColor, redColor, greenColor, blueColor, cyanColor, magentaColor, yellowColor };
	for (i = 0; i < 160000L * SCALE; i++) {
		randRect(&r, 120, 90);
		ForeColor(colors[i % 7]);
		switch (i & 3) {
		case 0: PaintRect(&r); break;
		case 1: FillRect(&r, &qd.gray); break;
		case 2: FrameRect(&r); break;
		default: InvertRect(&r); break;
		}
	}
	return hashWindow();
}

static unsigned long tOvals(void)
{
	long i; Rect r;
	for (i = 0; i < 40000L * SCALE; i++) {
		randRect(&r, 150, 110);
		if (i & 1) PaintOval(&r); else { FrameRoundRect(&r, 16, 16); FillOval(&r, &qd.ltGray); }
	}
	return hashWindow();
}

static unsigned long tLines(void)
{
	long i;
	MoveTo(300, 200);
	for (i = 0; i < 120000L * SCALE; i++) {
		if ((i & 255) == 0) PenSize(1 + (i >> 8) % 3, 1 + (i >> 8) % 3);
		LineTo(rnd() % 600, rnd() % 400);
	}
	return hashWindow();
}

static unsigned long tText(void)
{
	static const short fonts[] = { 0, 3, 4, 20, 22 };	/* system, Geneva, Monaco, Times, Courier */
	static const short sizes[] = { 9, 10, 12, 14, 18, 24 };
	long i;
	for (i = 0; i < 6000L * SCALE; i++) {
		/* Chicago exists at 12 points only. Any other size is a strike the Font
		   Manager scales and caches in the system heap; when that heap is full it
		   draws from the 12 point strike instead, a few pixels differently, and
		   the check then tells two heaps apart rather than two machines. */
		TextFont(fonts[i % 5]); TextSize(fonts[i % 5] ? sizes[i % 6] : 12); TextFace((i / 30) & 3);
		MoveTo(4 + rnd() % 200, 20 + rnd() % 370);
		DrawString("\pThe quick brown fox jumps over the lazy dog 0123456789");
	}
	TextFont(0); TextSize(0); TextFace(0);
	return hashWindow();
}

static unsigned long tRegions(void)
{
	long i; Rect r; RgnHandle a = NewRgn(), b = NewRgn(), c = NewRgn();
	for (i = 0; i < 10000L * SCALE; i++) {
		randRect(&r, 200, 150); OpenRgn(); FrameOval(&r); CloseRgn(a);
		randRect(&r, 200, 150); RectRgn(b, &r);
		switch (i % 3) { case 0: UnionRgn(a, b, c); break; case 1: DiffRgn(a, b, c); break; default: XorRgn(a, b, c); }
		if (i & 1) PaintRgn(c); else FillRgn(c, &qd.dkGray);
	}
	DisposeRgn(a); DisposeRgn(b); DisposeRgn(c);
	return hashWindow();
}

static void scene(short variant)
{
	Rect r; short i;
	for (i = 0; i < 40; i++) {
		SetRect(&r, i * 14, (i * 37 + variant * 53) % 300, i * 14 + 90, (i * 37 + variant * 53) % 300 + 80);
		ForeColor((i + variant) & 1 ? blueColor : redColor);
		if (i % 3 == 0) PaintOval(&r); else if (i % 3 == 1) FillRect(&r, &qd.gray); else FrameRoundRect(&r, 12, 12);
	}
	ForeColor(blackColor);
	/* The system font at its own size, for the reason given in tText: a size
	   the Font Manager has to scale is drawn one of two ways, as its heap allows. */
	TextSize(12); MoveTo(20, 380); DrawString(variant ? "\pSecond picture" : "\pFirst picture"); TextSize(0);
}

static unsigned long tCopyBits(void)
{
	long i; Rect s, d;
	scene(0);
	for (i = 0; i < 9000L * SCALE; i++) {
		SetRect(&s, 0, 0, 300, 200);
		SetRect(&d, 300 * (i & 1), 200, 300 * (i & 1) + 300, 400);
		CopyBits(&win->portBits, &win->portBits, &s, &d, (i & 2) ? srcXor : srcCopy, 0L);
	}
	return hashWindow();
}

static unsigned long tStretch(void)
{
	long i; Rect s, d;
	scene(1);
#ifdef STRETCH_DEBUG
	putPart("x_scene", hashWindow());
	putPart("x_scene_src", hashPart(0, 0, 150, 200));
	putPart("x_scene_text", hashPart(350, 0, 400, 600));
#endif
	for (i = 0; i < 1200L * SCALE; i++) {
		SetRect(&s, 0, 0, 200, 150);
		SetRect(&d, 200, 100, 200 + 250 + (i % 7) * 20, 100 + 160 + (i % 5) * 20);
		CopyBits(&win->portBits, &win->portBits, &s, &d, srcCopy, 0L);
#ifdef STRETCH_DEBUG
		if (i < 35) putPart("x_pass", hashPart(100, 200, 340, 570));	/* every size once */
#endif
	}
#ifdef STRETCH_DEBUG
	putPart("x_end_src", hashPart(0, 0, 150, 200));
	putPart("x_end_dst", hashPart(100, 200, 340, 570));
	putPart("x_end_text", hashPart(350, 0, 400, 600));
#endif
	return hashWindow();
}

static unsigned long tMono(void)
{
	/* A 1-bit image expanded to the screen's depth: icons, text, old applications */
	static BitMap bm; long i; Rect d; short y;
	bm.rowBytes = 64; SetRect(&bm.bounds, 0, 0, 512, 128);
	bm.baseAddr = (Ptr)bufA;
	for (i = 0; i < 2400L * SCALE; i++) {
		y = (i * 7) % 270;
		SetRect(&d, 40, y, 552, y + 128);
		ForeColor(i & 1 ? blueColor : blackColor);
		CopyBits(&bm, &win->portBits, &bm.bounds, &d, (i & 4) ? srcOr : srcCopy, 0L);
	}
	return hashWindow();
}

static unsigned long tScroll(void)
{
	long i; RgnHandle upd = NewRgn(); Rect r = win->portRect;
	scene(0);
	for (i = 0; i < 5000L * SCALE; i++) {
		ScrollRect(&r, (i & 64) ? 3 : -3, (i & 128) ? -4 : 4, upd);
		FillRgn(upd, (i & 1) ? &qd.ltGray : &qd.gray);
	}
	DisposeRgn(upd);
	return hashWindow();
}

static unsigned long tPictures(void)
{
	PicHandle pic[2]; long i; short k; Rect frame = win->portRect;
	for (k = 0; k < 2; k++) {
		pic[k] = OpenPicture(&frame);
		scene(k);
		ClosePicture();
	}
	for (i = 0; i < 1200L * SCALE; i++)
		DrawPicture(pic[i & 1], &frame);
	KillPicture(pic[0]); KillPicture(pic[1]);
	return hashWindow();
}

static unsigned long tWindows(void)
{
	long i; unsigned long h = 0;
	for (i = 0; i < 600L * SCALE; i++) {
		Rect r; WindowPtr w;
		SetRect(&r, 60 + (i % 8) * 20, 80 + (i % 5) * 20, 400 + (i % 8) * 20, 330 + (i % 5) * 20);
		w = NewWindow(0L, &r, "\pA window", true, (i & 1) ? documentProc : dBoxProc, (WindowPtr)-1L, true, 0);
		SetPort(w);
		scene(i & 1);
		if (i & 2) { MoveWindow(w, r.left + 40, r.top + 30, false); }
		DisposeWindow(w);
		SetPort(win);
		/* the window behind has lost what was under the other: redraw as an update would */
		BeginUpdate(win); EraseRect(&win->portRect); scene(0); EndUpdate(win);
	}
	h = hashWindow();
	return h;
}

static unsigned long tTextEdit(void)
{
	Rect r = win->portRect; TEHandle te; long i;
	InsetRect(&r, 8, 8);
	te = TENew(&r, &r);
	for (i = 0; i < 120; i++)
		TEInsert("Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor. ", 80, te);
	for (i = 0; i < 6000L * SCALE; i++) {
		TEScroll(0, (i & 32) ? 12 : -12, te);
		if ((i & 15) == 0) { TESetSelect(i * 3, i * 3 + 200, te); TEActivate(te); }
	}
	TEDispose(te);
	return hashWindow();
}

/* ---- results ---- */

static void writeResults(void)
{
	HParamBlockRec pb; Str255 name; short vref = 0, i, ref; long count = outLen;
	for (i = 1; i < 32; i++) {
		memset(&pb, 0, sizeof pb);
		pb.volumeParam.ioNamePtr = name;
		pb.volumeParam.ioVolIndex = i;
		if (PBHGetVInfoSync(&pb) != noErr) break;
		if (name[0] == 4 && memcmp(name + 1, "Unix", 4) == 0) { vref = pb.volumeParam.ioVRefNum; break; }
	}
#ifdef APPEND_RESULTS	/* a machine run by hand: every run is kept, one block after the other */
	{ OSErr err = HCreate(vref, vref ? 2 : 0, "\pMacBench Results", 'ttxt', 'TEXT'); if (err != noErr && err != dupFNErr) return; }
	if (HOpen(vref, vref ? 2 : 0, "\pMacBench Results", fsWrPerm, &ref) != noErr) return;
	SetFPos(ref, fsFromLEOF, 0);
#else
	HDelete(vref, vref ? 2 : 0, "\pMacBench Results");
	if (HCreate(vref, vref ? 2 : 0, "\pMacBench Results", 'ttxt', 'TEXT') != noErr) return;
	if (HOpen(vref, vref ? 2 : 0, "\pMacBench Results", fsWrPerm, &ref) != noErr) return;
#endif
	FSWrite(ref, &count, out);
	FSClose(ref);
	FlushVol(0L, vref);
}

int main(void)
{
	long i;
	InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(0L); InitCursor();
	FlushEvents(everyEvent, 0);

	bufA = (unsigned char *)NewPtr(BUF); bufB = (unsigned char *)NewPtr(BUF + 1024); bufC = (unsigned char *)NewPtr(BUF * 2 + 1024);
	if (!bufA || !bufB || !bufC) ExitToShell();
	rngState = 99;
	/* Data that compresses as text does: words drawn from a small vocabulary */
	for (i = 0; i < BUF; ) {
		static const char *words[] = { "window ", "the ", "Macintosh ", "of ", "QuickDraw ", "and ", "memory ", "handle ", "a ", "resource " };
		const char *w = words[rnd() % 10];
		while (*w && i < BUF) bufA[i++] = *w++;
	}
	packedLen = lzPack(bufA, BUF, bufC);

#ifdef DEPTH	/* run at this many bits a pixel, when the screen can */
	{ GDHandle screen = GetMainDevice(); if (HasDepth(screen, DEPTH, 0, 0)) SetDepth(screen, DEPTH, 0, 0); }
#endif
	win = NewWindow(0L, &winRect, "\pMacBench", true, noGrowDocProc, (WindowPtr)-1L, false, 0);
	SetPort(win);
	{ EventRecord e; for (i = 0; i < 8; i++) WaitNextEvent(everyEvent, &e, 2, 0L); BeginUpdate(win); EndUpdate(win); }
	winRect = win->portRect;
	LocalToGlobal((Point *)&winRect.top); LocalToGlobal((Point *)&winRect.bottom);
	HideCursor();

	put("MacBench 1 scale "); putNum(SCALE); put(" packed "); putNum(packedLen);
#ifdef DEPTH
	put(" depth "); putNum((**(**GetMainDevice()).gdPMap).pixelSize);
#endif
	put("\r");
	run("cpu_sieve", tSieve);
	run("cpu_sort", tSort);
	run("cpu_copy", tCopy);
	run("cpu_crc", tCrc);
	run("cpu_unpack", tUnpack);
	run("tb_packbits", tPackBits);
	run("tb_memory", tMemory);
	run("tb_strings", tStrings);
	run("qd_rects", tRects);
	run("qd_ovals", tOvals);
	run("qd_lines", tLines);
	run("qd_text", tText);
	run("qd_regions", tRegions);
	run("qd_copybits", tCopyBits);
	run("qd_stretch", tStretch);
	run("qd_mono", tMono);
	run("qd_scroll", tScroll);
	run("qd_pictures", tPictures);
	run("qd_windows", tWindows);
	run("qd_textedit", tTextEdit);
	{
		long fpuType = 0;
		if (Gestalt(gestaltFPUType, &fpuType) == noErr && fpuType != 0) {
			run("fpu_fft", tFpuFft);
			run("fpu_whet", tFpuWhet);
			run("fpu_matrix", tFpuMatrix);
			run("fpu_trig", tFpuTrig);
			run("fpu_move", tFpuMove);
		}
	}
	put("END\r");
	writeResults();
#ifdef HOLD_TICKS	/* leave the last drawing on screen for a while, to be looked at */
	{ unsigned long until = TickCount() + HOLD_TICKS; while (TickCount() < until) ; }
#endif
	ShowCursor();
	ExitToShell();
	return 0;
}
