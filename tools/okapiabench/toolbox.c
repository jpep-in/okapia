/*
 *  toolbox.c - OkapiaBench's tests of the Toolbox outside QuickDraw
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  On a PowerPC Macintosh part of the Toolbox is still 68k code, run by the
 *  ROM's own emulator: these measure the system, not the processor alone.
 */

#include <Resources.h>
#include <Events.h>
#include <Fonts.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <string.h>

#include "bench.h"

unsigned long tPackBits(void)
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

unsigned long tMemory(void)
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

unsigned long tStrings(void)
{
	long iter; Str255 s; long v; unsigned long h = 0;
	for (iter = 0; iter < 2000000L * SCALE; iter++) {
		NumToString((long)rnd(), s);
		StringToNum(s, &v);
		h += v + s[0];
	}
	return h;
}

/* ---- traps that do almost nothing: the cost of the dispatch itself ---- */

unsigned long tTraps(void)
{
	long i; unsigned long h = 0; GrafPtr port;
	for (i = 0; i < 6400000L * SCALE; i++) {
		THz z = GetZone();			/* an OS trap */
		GetPort(&port);				/* a Toolbox trap */
		SetPort(port);
		h += (unsigned long)(z != 0) + (port == (GrafPtr)win);
	}
	return h;
}

/* ---- the Resource Manager: the application's own resources and the System's ---- */

unsigned long tResources(void)
{
	long i, k; unsigned long h = 0;
	short n = Count1Resources('OKBD'), fonds;
	SetResLoad(true);
	for (i = 0; i < 4000L * SCALE; i++) {
		for (k = 0; k < 8; k++) {
			Handle r = Get1Resource('OKBD', 128 + k);
			if (r) { h = hashBytes(h, (const unsigned char *)*r, 16); ReleaseResource(r); }
		}
		h += Count1Resources('OKBD') + n;
	}
	fonds = CountResources('FOND');
	for (i = 0; i < 200L * SCALE; i++)
		for (k = 1; k <= fonds && k <= 16; k++) {
			Handle r = GetIndResource('FOND', k);	/* never released: the Font Manager may hold it */
			if (r) h += GetHandleSize(r) & 1;
		}
	return h;
}

/* ---- the Event Manager with nothing to do ---- */

unsigned long tEvents(void)
{
	long i, other = 0; EventRecord e;
	for (i = 0; i < 3000L * SCALE; i++) {
		if (WaitNextEvent(everyEvent & ~highLevelEventMask, &e, 0, 0L) && e.what != nullEvent) other++;
		EventAvail(everyEvent, &e);
	}
	if (other) extraNum("events=", other);
	/* The other processes decide what happens in between: the check is the
	   work asked for, not what came back. */
	return 3000UL * SCALE;
}

/* ---- the Font Manager measuring text, without drawing it ---- */

unsigned long tTextWidth(void)
{
	static const short fonts[] = { kGeneva, kMonaco, kTimes, kHelvetica, kCourier };
	static const short sizes[] = { 9, 10, 12, 14, 18, 24 };
	static const char s[] = "The quick brown fox jumps over the lazy dog 0123456789";
	long i; unsigned long h = 0; short c;
	for (i = 0; i < 20000L * SCALE; i++) {
		TextFont(fonts[i % 5]); TextSize(sizes[i % 6]); TextFace((i / 30) & 3);
		h += TextWidth((Ptr)s, 0, sizeof s - 1);
		h += StringWidth("\pHello, Macintosh");
		for (c = 'a'; c <= 'z'; c += 5) h += CharWidth(c);
	}
	TextFont(kGeneva); TextSize(12); TextFace(0);
	return h;
}
