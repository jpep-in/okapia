/*
 *  emb_port.c - Embench-IoT's nineteen programs in OkapiaBench (emb_*)
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Embench (external/embench-iot, GPL-3.0-or-later) builds one executable a
 *  program; here they share one application. Each is compiled as it is,
 *  with its four entry points renamed emb_<name>_... by the build, and run
 *  as Embench's support/main.c runs it: initialise_benchmark, warm_caches,
 *  then benchmark() timed alone and its result given to verify_benchmark.
 *  The check is that verdict.
 *
 *  The global scale factor starts at 1 and grows until a run lasts half a
 *  second; it is written after the check (gsf=), and the score Embench
 *  defines, reference time / time * gsf against baseline-data/speed.json,
 *  is computed by run.py and the recap. A port has no board: the board
 *  functions of support.h exist for the declarations only.
 */

#include <string.h>

#include "bench.h"

unsigned int emb_gsf = 1;

void initialise_board(void) {}
void start_trigger(void) {}
void stop_trigger(void) {}

#define EMB(id) \
	int emb_##id##_benchmark(void); void emb_##id##_initialise_benchmark(void); \
	void emb_##id##_warm_caches(int); int emb_##id##_verify_benchmark(int);
#define EMB_LIST \
	X(aha_mont64, "aha-mont64") X(crc32, "crc32") X(depthconv, "depthconv") X(edn, "edn") \
	X(huffbench, "huffbench") X(matmult_int, "matmult-int") X(md5sum, "md5sum") X(nettle_aes, "nettle-aes") \
	X(nettle_sha256, "nettle-sha256") X(nsichneu, "nsichneu") X(picojpeg, "picojpeg") X(qrduino, "qrduino") \
	X(sglib_combined, "sglib-combined") X(slre, "slre") X(statemate, "statemate") X(tarfind, "tarfind") \
	X(ud, "ud") X(wikisort, "wikisort") X(xgboost, "xgboost")

#define X(id, name) EMB(id)
EMB_LIST
#undef X

typedef struct {
	const char *name;
	int (*benchmark)(void);
	void (*initialise)(void);
	void (*warm)(int);
	int (*verify)(int);
} EmbProgram;

#define X(id, name) { name, emb_##id##_benchmark, emb_##id##_initialise_benchmark, emb_##id##_warm_caches, emb_##id##_verify_benchmark },
static const EmbProgram programs[] = { EMB_LIST };
#undef X

static const EmbProgram *current;

static unsigned long runCurrent(void)
{
	volatile int result = current->benchmark();
	g_checkIsVerdict = 1;
	extraNum("gsf=", emb_gsf);
	return current->verify(result) != 0;
}

void embRunAll(void)
{
	unsigned short i;
	for (i = 0; i < sizeof programs / sizeof programs[0]; i++) {
		char name[24];
		unsigned long t = 0;
		current = &programs[i];
		strcpy(name, "emb_"); strcat(name, current->name);
#ifdef ONLY
		if (strcmp(name, ONLY_NAME) != 0) continue;
#endif
		current->initialise();
		current->warm(1);
		/* size the run to about 0.6 s: measure short runs, ten times longer
		   each, until one lasts 50 ms, then scale from it */
		for (emb_gsf = 1; ; emb_gsf *= 10) {
			t = benchMicros();
			(void)current->benchmark();
			t = benchMicros() - t;
			if (t >= 50000UL || emb_gsf >= 100000u) break;
		}
		if (t < 500000UL && t) emb_gsf = (unsigned int)((unsigned long long)emb_gsf * 600000UL / t);
		run(name, runCurrent);
	}
	emb_gsf = 1;
}
