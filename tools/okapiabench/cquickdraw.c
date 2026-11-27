/*
 *  cquickdraw.c - OkapiaBench's Color QuickDraw tests
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  What colour applications and games of the 1990s asked of QuickDraw:
 *  arbitrary RGB colours, offscreen GWorlds copied to the screen, depth
 *  conversions, masked sprites, arithmetic transfer modes and colour
 *  patterns. Run only with 32-bit QuickDraw (okapiabench.c); each returns a
 *  hash of the window.
 */

#include <string.h>

#include "bench.h"

static void randRectIn(Rect *r, short w0, short h0, short maxW, short maxH)
{
	short w = 4 + rnd() % maxW, h = 4 + rnd() % maxH;
	r->left = rnd() % (w0 - w); r->top = rnd() % (h0 - h);
	r->right = r->left + w; r->bottom = r->top + h;
}

static void randColor(RGBColor *c)
{
	unsigned long v = rnd();
	c->red = (v & 0xff) * 257; c->green = ((v >> 8) & 0xff) * 257; c->blue = (rnd() & 0xff) * 257;
}

static BitMap *bitsOf(GWorldPtr gw) { return &((GrafPtr)gw)->portBits; }

/* A GWorld of the window's size at `depth` (0: the screen's), drawn with `variant` */
static GWorldPtr offscreen(short depth, short variant)
{
	GWorldPtr gw = 0; CGrafPtr port; GDHandle dev;
	Rect r = win->portRect;
	if (NewGWorld(&gw, depth, &r, 0L, 0L, 0) != noErr || !gw) return 0;
	LockPixels(GetGWorldPixMap(gw));
	GetGWorld(&port, &dev);
	SetGWorld((CGrafPtr)gw, 0L);
	PenNormal(); ForeColor(blackColor); BackColor(whiteColor);
	EraseRect(&r);
	scene(variant);
	SetGWorld(port, dev);
	return gw;
}

/* ---- arbitrary colours: the inverse table at 8 bits, direct at 16 and 32 ---- */

unsigned long tRGB(void)
{
	long i; Rect r; RGBColor c;
	for (i = 0; i < 180000L * SCALE; i++) {
		randRectIn(&r, 600, 400, 120, 90);
		randColor(&c);
		RGBForeColor(&c);
		if (i & 1) PaintRect(&r); else PaintOval(&r);
	}
	ForeColor(blackColor);
	return hashWindow();
}

/* ---- double buffering: draw offscreen, copy the whole picture on screen ---- */

unsigned long tGWorld(void)
{
	long i; Rect r, all = win->portRect; RGBColor c;
	CGrafPtr port; GDHandle dev;
	GWorldPtr gw = offscreen(0, 0);
	if (!gw) { strcpy(g_extra, "nogworld"); return 0; }
	GetGWorld(&port, &dev);
	for (i = 0; i < 7500L * SCALE; i++) {
		short k;
		SetGWorld((CGrafPtr)gw, 0L);
		for (k = 0; k < 8; k++) {		/* a few sprites moved */
			randRectIn(&r, 600, 400, 60, 40);
			randColor(&c); RGBForeColor(&c);
			PaintRect(&r);
		}
		ForeColor(blackColor);
		SetGWorld(port, dev);
		CopyBits(bitsOf(gw), &win->portBits, &all, &all, srcCopy, 0L);
	}
	DisposeGWorld(gw);
	return hashWindow();
}

/* ---- depth conversion: 8 and 32-bit pictures copied to whatever the screen is ---- */

unsigned long tConvert(void)
{
	long i; Rect all = win->portRect;
	GWorldPtr g8 = offscreen(8, 0), g32 = offscreen(32, 1);
	if (!g8 || !g32) { if (g8) DisposeGWorld(g8); if (g32) DisposeGWorld(g32); strcpy(g_extra, "nogworld"); return 0; }
	for (i = 0; i < 100L * SCALE; i++) {
		GWorldPtr g = (i & 1) ? g32 : g8;
		CopyBits(bitsOf(g), &win->portBits, &all, &all, (i & 2) ? srcCopy + 64 /* ditherCopy */ : srcCopy, 0L);
	}
	/* The check leaves the dithering out: QuickDraw carries a dither's error
	   from one call to the next, so the same calls draw differently each time
	   (with or without the translator alike). One plain copy of each half, last. */
	{
		Rect top = all, bottom = all;
		top.bottom = bottom.top = (all.top + all.bottom) / 2;
		CopyBits(bitsOf(g8), &win->portBits, &top, &top, srcCopy, 0L);
		CopyBits(bitsOf(g32), &win->portBits, &bottom, &bottom, srcCopy, 0L);
	}
	DisposeGWorld(g8); DisposeGWorld(g32);
	return hashWindow();
}

/* ---- sprites through a mask ---- */

unsigned long tMask(void)
{
	static unsigned char maskBits[64 * 64 / 8];
	BitMap mask; Rect s, d; long i;
	CGrafPtr port; GDHandle dev; GWorldPtr spr = 0;
	RGBColor c = { 0xffff, 0x8000, 0x0000 };
	SetRect(&s, 0, 0, 64, 64);
	if (NewGWorld(&spr, 0, &s, 0L, 0L, 0) != noErr || !spr) { strcpy(g_extra, "nogworld"); return 0; }
	LockPixels(GetGWorldPixMap(spr));
	GetGWorld(&port, &dev);
	SetGWorld((CGrafPtr)spr, 0L);
	EraseRect(&s); RGBForeColor(&c); PaintOval(&s); ForeColor(blueColor); FrameRect(&s); ForeColor(blackColor);
	SetGWorld(port, dev);
	/* the mask: a disc, built by hand so that it needs no port */
	mask.baseAddr = (Ptr)maskBits; mask.rowBytes = 8; mask.bounds = s;
	{
		short x, y;
		memset(maskBits, 0, sizeof maskBits);
		for (y = 0; y < 64; y++)
			for (x = 0; x < 64; x++)
				if ((x - 32) * (x - 32) + (y - 32) * (y - 32) < 30 * 30) maskBits[y * 8 + x / 8] |= 0x80 >> (x & 7);
	}
	scene(0);
	for (i = 0; i < 40000L * SCALE; i++) {
		short x = rnd() % (600 - 64), y = rnd() % (400 - 64);
		SetRect(&d, x, y, x + 64, y + 64);
		if (i & 1) CopyMask(bitsOf(spr), &mask, &win->portBits, &s, &s, &d);
		else CopyDeepMask(bitsOf(spr), &mask, &win->portBits, &s, &s, &d, srcCopy, 0L);
	}
	DisposeGWorld(spr);
	return hashWindow();
}

/* ---- the arithmetic transfer modes ---- */

unsigned long tModes(void)
{
	static const short modes[] = { blend, addOver, subPin, adMin, addPin, subOver, adMax };
	long i; Rect r; RGBColor c, op = { 0x8000, 0x8000, 0x8000 };
	scene(1);
	OpColor(&op);
	for (i = 0; i < 15000L * SCALE; i++) {
		randRectIn(&r, 600, 400, 120, 90);
		randColor(&c); RGBForeColor(&c);
		PenMode(modes[i % 7]);
		PaintRect(&r);
	}
	PenNormal(); ForeColor(blackColor);
	return hashWindow();
}

/* ---- colour patterns ---- */

unsigned long tPixPat(void)
{
	PixPatHandle pp[4]; long i; Rect r; short k;
	for (k = 0; k < 4; k++) {
		RGBColor c;
		pp[k] = NewPixPat();
		if (!pp[k]) { while (k--) DisposePixPat(pp[k]); strcpy(g_extra, "nopixpat"); return 0; }
		randColor(&c);
		MakeRGBPat(pp[k], &c);	/* a colour QuickDraw dithers into an 8 by 8 pattern */
	}
	for (i = 0; i < 120000L * SCALE; i++) {
		randRectIn(&r, 600, 400, 120, 90);
		if (i & 1) FillCRect(&r, pp[i & 3]); else FillCOval(&r, pp[(i >> 1) & 3]);
	}
	for (k = 0; k < 4; k++) DisposePixPat(pp[k]);
	return hashWindow();
}
