/*
 *  bench.c - OkapiaBench's run: time a test, keep its line
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Each test is a fixed amount of work, timed with the Time Manager's
 *  Microseconds and, beside it, TickCount: a test whose two clocks disagree
 *  ran on a Macintosh whose clock drifts (run.py says so). A line is
 *  "name microseconds ticks check [extra]".
 */

#include <string.h>
#include <OSUtils.h>
#include <Traps.h>

#include "bench.h"

char g_out[16384];
long g_outLen;
char g_suffix[8];
char g_extra[40];
int g_checkIsVerdict;
Result g_results[MAX_RESULTS];
short g_resultCount;
unsigned long rngState;

void put(const char *s) { while (*s && g_outLen < (long)sizeof g_out - 1) g_out[g_outLen++] = *s++; }
void putPStr(const unsigned char *s) { short i; for (i = 1; i <= s[0] && g_outLen < (long)sizeof g_out - 1; i++) g_out[g_outLen++] = s[i]; }
void putNum(unsigned long v)
{
	char b[12]; int i = 11;
	b[i] = 0;
	do { b[--i] = '0' + v % 10; v /= 10; } while (v);
	put(b + i);
}
void putHex(unsigned long v)
{
	static const char d[] = "0123456789abcdef";
	char b[9]; int i;
	for (i = 7; i >= 0; i--) { b[i] = d[v & 15]; v >>= 4; }
	b[8] = 0;
	put(b);
}

void extraNum(const char *label, unsigned long v)
{
	char d[12]; int i = 11, n = strlen(g_extra);
	d[i] = 0;
	do { d[--i] = '0' + v % 10; v /= 10; } while (v);
	if (n && n < (int)sizeof g_extra - 1) g_extra[n++] = ' ';
	g_extra[n] = 0;
	strncat(g_extra, label, sizeof g_extra - 1 - strlen(g_extra));
	strncat(g_extra, d + i, sizeof g_extra - 1 - strlen(g_extra));
}

unsigned long rnd(void)
{
	rngState = rngState * 1103515245UL + 12345UL;
	return rngState >> 8;
}

unsigned long hashBytes(unsigned long h, const unsigned char *p, long n)
{
	while (n--) h = (h ^ *p++) * 16777619UL;
	return h;
}

/* What is on screen inside the window, read straight from the frame buffer.
   Below eight bits a pixel is part of a byte: the bytes the window's columns
   fall in are hashed whole, neighbours included. */
unsigned long hashWindow(void)
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

/* The Time Manager's microsecond count, trap A193 (System 7). Its result is
   in A0 (high) and D0 (low); the three words after the trap store them. */
typedef struct { unsigned long hi, lo; } MicroCount;	/* UnsignedWide, which these headers lack */
#if defined(__ppc__)
pascal void Microseconds(MicroCount *t);	/* InterfaceLib */
#else
pascal void Microseconds(MicroCount *t) M68K_INLINE(0xA193, 0x225F, 0x22C8, 0x2280);
#endif

static int haveMicroseconds = -1;

unsigned long benchMicros(void)
{
	MicroCount w;
	if (haveMicroseconds < 0) {
#if defined(__ppc__)
		haveMicroseconds = 1;
#else
		haveMicroseconds = NGetTrapAddress(0xA193, kOSTrapType) != NGetTrapAddress(_Unimplemented, kToolboxTrapType);
#endif
		if (haveMicroseconds) {
			/* A trap there is not enough: under System 7.1 it answers, and
			   always the same. It must move by about a tick in a tick. */
			unsigned long t = TickCount(), a;
			MicroCount w;
			while (TickCount() == t) ;
			Microseconds(&w); a = w.lo; t = TickCount();
			while (TickCount() == t) ;
			Microseconds(&w);
			haveMicroseconds = w.lo - a > 8000 && w.lo - a < 40000;
		}
	}
	if (!haveMicroseconds) return TickCount() * 16626UL;	/* a tick is 16,625.8 µs */
	Microseconds(&w);
	return w.lo;	/* differences only: a wrap every 71 minutes cancels out */
}

int benchHasMicroseconds(void) { if (haveMicroseconds < 0) benchMicros(); return haveMicroseconds; }

static void record(const char *name, unsigned long usec, unsigned long ticks, unsigned long check, short skipped)
{
	Result *r;
	if (g_resultCount >= MAX_RESULTS) return;
	r = &g_results[g_resultCount++];
	strncpy(r->name, name, sizeof r->name - 1); r->name[sizeof r->name - 1] = 0;
	strncat(r->name, g_suffix, sizeof r->name - 1 - strlen(r->name));
	r->usec = usec; r->ticks = ticks; r->check = check; r->skipped = skipped; r->verdict = g_checkIsVerdict;
	strncpy(r->extra, skipped ? "" : g_extra, sizeof r->extra - 1); r->extra[sizeof r->extra - 1] = 0;
}

void skip(const char *name, const char *why)
{
#ifdef ONLY
	if (strcmp(name, ONLY_NAME) != 0) return;
#endif
	put(name); put(g_suffix); put(" skip "); put(why); put("\r");
	g_checkIsVerdict = 0;
	record(name, 0, 0, 0, 1);
	resultsFlush();
}

void run(const char *name, TestProc proc)
{
	unsigned long t, u, check;
	Str255 title;
	int n = strlen(name);
#ifdef REPEAT
	int pass;
#endif
#ifdef ONLY
	if (strcmp(name, ONLY_NAME) != 0) return;	/* a build that runs one test, to look at it alone */
#endif
#ifdef REPEAT	/* the same test again and again: is its check the same every time? */
	for (pass = 0; pass < REPEAT; pass++) {
#endif
	title[0] = n; memcpy(title + 1, name, n);
	SetWTitle(win, title);
	SetPort(win);
	PenNormal(); ForeColor(blackColor); BackColor(whiteColor);
	TextFont(kGeneva); TextSize(12); TextFace(0);
	EraseRect(&win->portRect);
	rngState = 12345;
	g_extra[0] = 0;
	g_checkIsVerdict = 0;
	t = TickCount();
	while (TickCount() == t) ;		/* start on a tick edge */
	t = TickCount();
	u = benchMicros();
	check = proc();
	u = benchMicros() - u;
	t = TickCount() - t;
	put(name); put(g_suffix); put(" "); putNum(u); put(" "); putNum(t); put(" ");
	if (g_checkIsVerdict) put(check ? "ok" : "FAIL"); else putHex(check);
	if (g_extra[0]) { put(" "); put(g_extra); }
	put("\r");
	record(name, u, t, check, 0);
	resultsFlush();
#ifdef REPEAT
	}
#endif
}
