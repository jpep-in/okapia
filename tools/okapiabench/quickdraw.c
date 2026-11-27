/*
 *  quickdraw.c - OkapiaBench's QuickDraw tests
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Each draws in the window and returns a hash of what is on screen. Two
 *  machines agree on it only with the same QuickDraw: the Quadra ROM's and
 *  a Power Macintosh's native one may differ by a pixel.
 */

#include <Fonts.h>
#include <TextEdit.h>
#include <string.h>

#include "bench.h"

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
static void putPart(const char *name, unsigned long h) { put(name); put(" 0 0 "); putHex(h); put("\r"); }
#endif

static void randRect(Rect *r, short maxW, short maxH)
{
	short w = 4 + rnd() % maxW, h = 4 + rnd() % maxH;
	r->left = rnd() % (600 - w); r->top = rnd() % (400 - h);
	r->right = r->left + w; r->bottom = r->top + h;
}

unsigned long tRects(void)
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

unsigned long tOvals(void)
{
	long i; Rect r;
	for (i = 0; i < 40000L * SCALE; i++) {
		randRect(&r, 150, 110);
		if (i & 1) PaintOval(&r); else { FrameRoundRect(&r, 16, 16); FillOval(&r, &qd.ltGray); }
	}
	return hashWindow();
}

unsigned long tLines(void)
{
	long i;
	MoveTo(300, 200);
	for (i = 0; i < 120000L * SCALE; i++) {
		if ((i & 255) == 0) PenSize(1 + (i >> 8) % 3, 1 + (i >> 8) % 3);
		LineTo(rnd() % 600, rnd() % 400);
	}
	return hashWindow();
}

unsigned long tText(void)
{
	static const short fonts[] = { kGeneva, kMonaco, kTimes, kHelvetica, kCourier };
	static const short sizes[] = { 9, 10, 12, 14, 18, 24 };
	long i;
	for (i = 0; i < 6000L * SCALE; i++) {
		/* Never the system font: Chicago in one System, Charcoal in another,
		   and a size it lacks is scaled from a strike the system heap may or may
		   not keep, so the check would tell two heaps apart, not two machines.
		   On Mac OS 8.5 and later, font smoothing must be off for the same
		   reason (docs/contributing/benchmarks.md). */
		TextFont(fonts[i % 5]); TextSize(sizes[i % 6]); TextFace((i / 30) & 3);
		MoveTo(4 + rnd() % 200, 20 + rnd() % 370);
		DrawString("\pThe quick brown fox jumps over the lazy dog 0123456789");
	}
	TextFont(kGeneva); TextSize(12); TextFace(0);
	return hashWindow();
}

unsigned long tRegions(void)
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

void scene(short variant)
{
	Rect r; short i;
	for (i = 0; i < 40; i++) {
		SetRect(&r, i * 14, (i * 37 + variant * 53) % 300, i * 14 + 90, (i * 37 + variant * 53) % 300 + 80);
		ForeColor((i + variant) & 1 ? blueColor : redColor);
		if (i % 3 == 0) PaintOval(&r); else if (i % 3 == 1) FillRect(&r, &qd.gray); else FrameRoundRect(&r, 12, 12);
	}
	ForeColor(blackColor);
	/* Geneva at a size it has, for the reason given in tText */
	TextFont(kGeneva); TextSize(12); MoveTo(20, 380); DrawString(variant ? "\pSecond picture" : "\pFirst picture");
}

unsigned long tCopyBits(void)
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

unsigned long tStretch(void)
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

unsigned long tMono(void)
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

unsigned long tScroll(void)
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

unsigned long tPictures(void)
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

unsigned long tWindows(void)
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

unsigned long tTextEdit(void)
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
