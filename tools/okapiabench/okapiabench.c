/*
 *  okapiabench.c - OkapiaBench, a self-running benchmark for a 68k or a
 *                  PowerPC Macintosh
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  MIT rather than Okapia's GPL, so that anyone can take these files —
 *  Basilisk II, another emulator, a period machine's owner. The application
 *  as built also contains Embench (GPL-3.0-or-later) and CoreMark
 *  (Apache-2.0), and is distributed under the GPL for that reason; LICENSE,
 *  here, says which part is which. -DNO_EMBENCH builds it without Embench.
 *
 *  Runs a fixed list of tests, each a fixed amount of work, and writes one
 *  line a test to a new file "OkapiaBench <date> <time>" at the root of the
 *  folder an emulator shares with its host (Basilisk II's and SheepShaver's
 *  ExtFS volume, "Unix" or "Okapia"), or next to the application when there
 *  is none; then shows a recap until a
 *  click or a key. A line is "name microseconds ticks check [extra]";
 *  `check` is a hash of what the test computed or drew, or Embench's and
 *  CoreMark's own verdict: two machines that disagree on it did not do the
 *  same work. The header lines, "# key value", say what the machine is.
 *
 *  2026-11-27: was MacBench (tools/macbench/macbench.c); renamed, split into
 *  one file a family, and extended to the PowerPC, CoreMark, Embench and the
 *  tests of real work (docs/contributing/benchmarks.md#okapiabench).
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
#include <Gestalt.h>
#include <string.h>

#include "bench.h"

WindowPtr win;
Rect winRect;
static const Rect winBounds = { 40, 10, 440, 610 };
unsigned char *bufA, *bufB, *bufC;
long g_cpu, g_fpu, g_qd, g_sysVersion;
int g_colorQD;

static void makeWindow(void)
{
	EventRecord e; short i;
	if (win) DisposeWindow(win);
	win = g_colorQD ? NewCWindow(0L, &winBounds, "\pOkapiaBench", true, noGrowDocProc, (WindowPtr)-1L, false, 0)
	                : NewWindow(0L, &winBounds, "\pOkapiaBench", true, noGrowDocProc, (WindowPtr)-1L, false, 0);
	SetPort(win);
	for (i = 0; i < 8; i++) WaitNextEvent(everyEvent, &e, 2, 0L);
	BeginUpdate(win); EndUpdate(win);
	winRect = win->portRect;
	LocalToGlobal((Point *)&winRect.top); LocalToGlobal((Point *)&winRect.bottom);
}

/* ---- the header ---- */

static void putDec2(long v) { if (v < 10) put("0"); putNum(v); }

static void putDate(unsigned long secs, int forName)
{
	DateTimeRec d;
	SecondsToDate(secs, &d);
	putNum(d.year); put("-"); putDec2(d.month); put("-"); putDec2(d.day);
	put(forName ? " " : "T"); putDec2(d.hour);
	if (!forName) put(":");
	putDec2(d.minute);
	if (!forName) { put(":"); putDec2(d.second); }
}

static void putKey(const char *key) { put("# "); put(key); put(" "); }

static void header(unsigned long now)
{
	long v;
	PixMapHandle pm = (**GetMainDevice()).gdPMap;
	Rect sr = (**GetMainDevice()).gdRect;
	put("OkapiaBench "); putNum(OKAPIABENCH_VERSION); put("\r");
	putKey("date"); putDate(now, 0); put("\r");
#if defined(__ppc__)
	putKey("code"); put("ppc\r");
#else
	putKey("code"); put("68k\r");
#endif
	putKey("sysarch"); put(Gestalt(gestaltSysArchitecture, &v) == noErr && v == 2 ? "ppc" : "68k"); put("\r");
	if (Gestalt(gestaltMachineType, &v) == noErr) { putKey("machine"); putNum(v); put("\r"); }
	putKey("cpu"); putNum(g_cpu); put("\r");
	if (Gestalt(gestaltNativeCPUtype, &v) == noErr) { putKey("cput"); putHex(v); put("\r"); }
	putKey("fpu"); putNum(g_fpu); put("\r");
	putKey("system"); putHex(g_sysVersion & 0xffff); put("\r");
	if (Gestalt(gestaltPhysicalRAMSize, &v) == noErr) { putKey("ram"); putNum(v); put("\r"); }
	if (Gestalt(gestaltLogicalRAMSize, &v) == noErr) { putKey("lram"); putNum(v); put("\r"); }
	putKey("vm"); put(Gestalt(gestaltVMAttr, &v) == noErr && (v & 1) ? "on" : "off"); put("\r");
	putKey("qd"); putHex(g_qd); put("\r");
	if (Gestalt('qtim', &v) == noErr) { putKey("quicktime"); putHex(v); put("\r"); }
	putKey("screen"); putNum(sr.right - sr.left); put("x"); putNum(sr.bottom - sr.top); put("x"); putNum((**pm).pixelSize); put("\r");
	putKey("compiler"); put("GCC " __VERSION__); put("\r");
	putKey("scale"); putNum(SCALE); put("\r");
	putKey("clock"); put(benchHasMicroseconds() ? "microseconds" : "ticks"); put("\r");
}

/* ---- the results file ---- */

static short findShare(void)
{
	HParamBlockRec pb; Str255 name; short i;
	for (i = 1; i < 32; i++) {
		memset(&pb, 0, sizeof pb);
		pb.volumeParam.ioNamePtr = name;
		pb.volumeParam.ioVolIndex = i;
		if (PBHGetVInfoSync(&pb) != noErr) break;
		/* Basilisk II's and SheepShaver's ExtFS ('ba'), whatever its name:
		   "Unix" upstream, "Okapia" or the user's own on Okapia */
		if (pb.volumeParam.ioVFSID == 0x6261 || (name[0] == 4 && memcmp(name + 1, "Unix", 4) == 0))
			return pb.volumeParam.ioVRefNum;
	}
	return 0;
}

/* "OkapiaBench 2026-11-27 1432", then " b", " c"... if a run of the same minute
   is there. Created before the first test and written to after each one, so
   that a test which never returns leaves the lines of those before it. */
static short resultsRef, resultsVol;
static long resultsWritten;

static void openResults(unsigned long now, unsigned char *fileName)
{
	short k;
	unsigned char base;
	long saved = g_outLen;
	char *start = g_out + g_outLen;
	resultsVol = findShare();
	resultsRef = 0;
	put("OkapiaBench "); putDate(now, 1);
	fileName[0] = (unsigned char)(g_outLen - saved);
	memcpy(fileName + 1, start, fileName[0]);
	g_outLen = saved;
	base = fileName[0];
	for (k = 0; k < 26; k++) {
		OSErr err;
		fileName[0] = base;
		if (k) { fileName[++fileName[0]] = ' '; fileName[++fileName[0]] = 'a' + k; }
		err = HCreate(resultsVol, resultsVol ? 2 : 0, fileName, 'ttxt', 'TEXT');
		if (err == noErr) break;
		if (err != dupFNErr) return;
	}
	if (HOpen(resultsVol, resultsVol ? 2 : 0, fileName, fsWrPerm, &resultsRef) != noErr) resultsRef = 0;
	resultsWritten = 0;
}

/* what has been put since the last call, to the file */
void resultsFlush(void)
{
	long count = g_outLen - resultsWritten;
	if (!resultsRef || count <= 0) return;
	FSWrite(resultsRef, &count, g_out + resultsWritten);
	resultsWritten += count;
}

static void closeResults(void)
{
	resultsFlush();
	if (resultsRef) FSClose(resultsRef);
	resultsRef = 0;
	FlushVol(0L, resultsVol);
}

/* ---- the families ---- */

static void cpuFamily(void)
{
	run("cpu_sieve", tSieve);
	run("cpu_sort", tSort);
	run("cpu_copy", tCopy);
	run("cpu_crc", tCrc);
	run("cpu_unpack", tUnpack);
	run("cpu_calls", tCalls);
	run("cpu_muldiv", tMulDiv);
	run("cpu_recurse", tRecurse);
	run("cpu_interp", tInterp);
	run("cpu_chase_16k", tChase16k);
	run("cpu_chase_256k", tChase256k);
	run("cpu_chase_4m", tChase4m);
	run("cpu_stream", tStream);
	run("cpu_codegen", tCodegen);
	run("cpu_coremark", tCoreMark);
}

static void toolboxFamily(void)
{
	run("tb_packbits", tPackBits);
	run("tb_memory", tMemory);
	run("tb_strings", tStrings);
	run("tb_traps", tTraps);
	run("tb_resources", tResources);
	run("tb_events", tEvents);
	run("tb_textwidth", tTextWidth);
}

static void quickdrawFamily(void)
{
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
}

static void colorFamily(void)
{
	if (!g_colorQD) { skip("cqd_rgb", "no-32bit-qd"); return; }
	run("cqd_rgb", tRGB);
	run("cqd_gworld", tGWorld);
	run("cqd_convert", tConvert);
	run("cqd_mask", tMask);
	run("cqd_modes", tModes);
	run("cqd_pixpat", tPixPat);
}

static void floatFamily(void)
{
#if !defined(__ppc__)
	if (!g_fpu) { skip("fp_fft", "no-fpu"); return; }	/* fp.c is compiled for the 68881 */
#endif
	run("fp_fft", tFpFft);
	run("fp_matrix", tFpMatrix);
	run("fp_whet", tFpWhet);
	run("app_raytrace", tRaytrace);
#if !defined(__ppc__)
	run("fpu_whet", tFpuWhet);
	run("fpu_trig", tFpuTrig);
	run("fpu_move", tFpuMove);
	run("fpu_forms", tFpuForms);
	run("fpu_calls", tFpuCalls);
#endif
}

static void appFamily(void)
{
	if (!appsPrepare()) { skip("app_deflate", "nomem"); return; }
	run("app_deflate", tDeflate);
	run("app_unzip", tUnzip);
#if !defined(__ppc__)
	if (g_fpu)
#endif
		run("app_jpeg_enc", tJpegEnc);
	run("app_jpeg_dec", tJpegDec);
	run("app_png_dec", tPngDec);
	if (qtAvailable()) run("app_qt_jpeg", tQtJpeg); else skip("app_qt_jpeg", "no-quicktime");
	run("app_text", tText2);
	run("app_sheet", tSheet);
	run("app_adpcm", tAdpcm);
}

/* The drawing tests again at every depth the screen offers, named "@<bits>" */
static void depthSweep(void)
{
	static const short depths[] = { 1, 8, 16, 32 };
	GDHandle screen = GetMainDevice();
	short original = (**(**screen).gdPMap).pixelSize, k;
	if (!g_colorQD) return;
	for (k = 0; k < 4; k++) {
		short d = depths[k];
		if (!HasDepth(screen, d, 0, 0) || SetDepth(screen, d, 0, 0) != noErr) continue;
		makeWindow();
		g_suffix[0] = '@'; g_suffix[1] = 0;
		{ char b[4]; int i = 3; b[i] = 0; do { b[--i] = '0' + d % 10; d /= 10; } while (d); strcat(g_suffix, b + i); }
		run("qd_rects", tRects);
		run("qd_text", tText);
		run("qd_copybits", tCopyBits);
		run("qd_scroll", tScroll);
		run("cqd_rgb", tRGB);
		run("cqd_gworld", tGWorld);
		g_suffix[0] = 0;
	}
	if ((**(**screen).gdPMap).pixelSize != original) { SetDepth(screen, original, 0, 0); makeWindow(); }
}

int main(void)
{
	unsigned long now;
	Str63 fileName;
	InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(0L); InitCursor();
	FlushEvents(everyEvent, 0);

	if (Gestalt(gestaltProcessorType, &g_cpu) != noErr) g_cpu = 0;
	if (Gestalt(gestaltFPUType, &g_fpu) != noErr) g_fpu = 0;
	if (Gestalt(gestaltQuickdrawVersion, &g_qd) != noErr) g_qd = 0;
	if (Gestalt(gestaltSystemVersion, &g_sysVersion) != noErr) g_sysVersion = 0;
	g_colorQD = g_qd >= 0x200;	/* gestalt32BitQD */
	GetDateTime(&now);

#if !defined(__ppc__)
	if (g_cpu < 3) {	/* gestalt68020: the tests are compiled for it */
		openResults(now, fileName);
		header(now);
		put("needs a 68020 or later\rEND\r");
		closeResults();
		ExitToShell();
	}
#endif

	bufA = (unsigned char *)NewPtr(BUF); bufB = (unsigned char *)NewPtr(BUF + 1024); bufC = (unsigned char *)NewPtr(BUF * 2 + 1024);
	if (!bufA || !bufB || !bufC) ExitToShell();
	cpuPrepare();

#ifdef DEPTH	/* start at this many bits a pixel, when the screen can */
	{ GDHandle screen = GetMainDevice(); if (HasDepth(screen, DEPTH, 0, 0)) SetDepth(screen, DEPTH, 0, 0); }
#endif
	makeWindow();
	HideCursor();

	openResults(now, fileName);
	header(now);
	resultsFlush();
	cpuFamily();
	toolboxFamily();
	quickdrawFamily();
	colorFamily();
	floatFamily();
	appFamily();
#ifndef NO_EMBENCH
	embRunAll();
#endif
	depthSweep();
	put("END\r");
	closeResults();
	ShowCursor();
#ifndef NO_RECAP
	recap(fileName);
#endif
	ExitToShell();
	return 0;
}
