/*
 *  bench.h - what OkapiaBench's test files share: the run, the hashes, the
 *            window and the buffers
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 */
#ifndef OKAPIABENCH_BENCH_H
#define OKAPIABENCH_BENCH_H

#include <Quickdraw.h>
#include <Windows.h>
#include <Memory.h>
#include <Gestalt.h>

#ifndef SCALE
#define SCALE 1		/* multiplies every iteration count */
#endif

#define OKAPIABENCH_VERSION 2

/* -DONLY=cpu_crc runs that test alone (a bare name: CMake's flags lose quotes) */
#define OKB_STR2(x) #x
#define OKB_STR(x) OKB_STR2(x)
#define ONLY_NAME OKB_STR(ONLY)

typedef unsigned long (*TestProc)(void);

/* ---- the run (bench.c) ---- */

void run(const char *name, TestProc proc);
unsigned long benchMicros(void);	/* the Time Manager's microseconds, or ticks scaled */
int benchHasMicroseconds(void);		/* 0: the times are ticks scaled (System 7.1) */
void skip(const char *name, const char *why);
extern char g_suffix[8];		/* appended to every name: "@16" in the depth sweep */
extern char g_extra[40];		/* a test may set it: written after its check */
extern int g_checkIsVerdict;		/* the check is a verdict: 1 ok, 0 FAIL */
void extraNum(const char *label, unsigned long v);	/* g_extra = label and number */

typedef struct {
	char name[24];
	unsigned long usec, ticks, check;
	short skipped, verdict;
	char extra[24];
} Result;
extern Result g_results[];
extern short g_resultCount;
#define MAX_RESULTS 192

/* ---- the output, a text kept in memory and written once at the end ---- */

void put(const char *s);
void putNum(unsigned long v);
void putHex(unsigned long v);
void putPStr(const unsigned char *s);
extern char g_out[];
extern long g_outLen;
void resultsFlush(void);	/* okapiabench.c: the lines so far, to the file */

/* ---- hashes and the random numbers every test starts from ---- */

extern unsigned long rngState;
unsigned long rnd(void);
unsigned long hashBytes(unsigned long h, const unsigned char *p, long n);
unsigned long hashWindow(void);

/* ---- the machine, from Gestalt ---- */

extern long g_cpu, g_fpu, g_qd, g_sysVersion;
extern int g_colorQD;		/* Color QuickDraw with 32-bit QuickDraw: GWorlds, RGBForeColor */

/* ---- the window and the buffers ---- */

extern WindowPtr win;
extern Rect winRect;		/* the window's content, in global coordinates */
extern unsigned char *bufA, *bufB, *bufC;
#define BUF 65536L
void scene(short variant);	/* quickdraw.c: a picture to copy, scroll, record */

/* Fonts drawn by the tests: never the system font, which Mac OS 8's Appearance
   turns from Chicago to Charcoal, so that every System draws the same text. */
enum { kGeneva = 3, kMonaco = 4, kTimes = 20, kHelvetica = 21, kCourier = 22 };

/* ---- the tests, by family ---- */

/* cpu.c */
unsigned long tSieve(void), tSort(void), tCopy(void), tCrc(void), tUnpack(void);
unsigned long tCalls(void), tMulDiv(void), tRecurse(void), tInterp(void);
unsigned long tChase16k(void), tChase256k(void), tChase4m(void), tStream(void), tCodegen(void);
void cpuPrepare(void);		/* the packed text cpu_unpack reads */
extern long packedLen;
/* coremark_port/core_portme.c */
unsigned long tCoreMark(void);
/* toolbox.c */
unsigned long tPackBits(void), tMemory(void), tStrings(void);
unsigned long tTraps(void), tResources(void), tEvents(void), tTextWidth(void);
/* quickdraw.c */
unsigned long tRects(void), tOvals(void), tLines(void), tText(void), tRegions(void);
unsigned long tCopyBits(void), tStretch(void), tMono(void), tScroll(void), tPictures(void);
unsigned long tWindows(void), tTextEdit(void);
/* cquickdraw.c */
unsigned long tRGB(void), tGWorld(void), tConvert(void), tMask(void), tModes(void), tPixPat(void);
/* fp.c */
unsigned long tFpFft(void), tFpMatrix(void), tFpWhet(void), tRaytrace(void);
/* fpu68881.c */
unsigned long tFpuWhet(void), tFpuTrig(void), tFpuMove(void), tFpuForms(void), tFpuCalls(void);
/* apps.c */
int appsPrepare(void);
unsigned long tDeflate(void), tUnzip(void), tJpegEnc(void), tJpegDec(void), tPngDec(void);
unsigned long tQtJpeg(void), tText2(void), tSheet(void), tAdpcm(void);
int qtAvailable(void);
/* emb_port.c */
void embRunAll(void);
/* recap.c */
void recap(const unsigned char *fileName);

#endif
