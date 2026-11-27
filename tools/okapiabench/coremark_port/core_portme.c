/*
 *  core_portme.c - CoreMark on a Macintosh: time, output, and the test
 *                  OkapiaBench runs (cpu_coremark)
 *
 *  Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Adapted from CoreMark's barebones/core_portme.c (external/coremark).
 *  Modified: the clock is the Time Manager's microsecond count (bench.c);
 *  ee_printf keeps CoreMark's report in memory, and tCoreMark reads back
 *  whether CoreMark validated its own results and how many iterations it ran
 *  in how long. The score, iterations per second, goes after the check.
 */

#include <stdarg.h>
#include <string.h>

#include "coremark.h"
#include "core_portme.h"
#include "../bench.h"

volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = 0;	/* ITERATIONS 0: CoreMark sizes the run to ten seconds or more */
volatile ee_s32 seed5_volatile = 0;

static CORETIMETYPE start_time_val, stop_time_val;
ee_u32 default_num_contexts = 1;

void start_time(void) { start_time_val = benchMicros(); }
void stop_time(void) { stop_time_val = benchMicros(); }
CORE_TICKS get_time(void) { return (CORE_TICKS)(stop_time_val - start_time_val); }
secs_ret time_in_secs(CORE_TICKS ticks) { return (secs_ret)(ticks / 1000000UL); }

void portable_init(core_portable *p, int *argc, char *argv[])
{
	(void)argc; (void)argv;
	if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) ee_printf("ERROR! Please define ee_ptr_int to a type that holds a pointer!\n");
	if (sizeof(ee_u32) != 4) ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
	p->portable_id = 1;
}

void portable_fini(core_portable *p) { p->portable_id = 0; }

/* ---- ee_printf: the few conversions CoreMark's report uses ---- */

static char report[2048];
static long reportLen;

static void out(char c) { if (reportLen < (long)sizeof report - 1) report[reportLen++] = c; }

static void outNum(unsigned long v, int base, int width, char pad)
{
	char b[12]; int i = 0;
	do { b[i++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
	while (i < width--) out(pad);
	while (i) out(b[--i]);
}

int ee_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	for (; *fmt; fmt++) {
		int width = 0; char pad = ' '; int isLong = 0;
		if (*fmt != '%') { out(*fmt); continue; }
		fmt++;
		if (*fmt == '0') { pad = '0'; fmt++; }
		while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
		if (*fmt == 'l') { isLong = 1; fmt++; }
		switch (*fmt) {
		case 'd': { long v = isLong ? va_arg(ap, long) : va_arg(ap, int); if (v < 0) { out('-'); v = -v; } outNum((unsigned long)v, 10, width, pad); break; }
		case 'u': outNum(isLong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int), 10, width, pad); break;
		case 'x': outNum(isLong ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int), 16, width, pad); break;
		case 's': { const char *s = va_arg(ap, const char *); while (*s) out(*s++); break; }
		case 'f': (void)va_arg(ap, double); out('?'); break;	/* not used with HAS_FLOAT 0 */
		case '%': out('%'); break;
		default: out('%'); out(*fmt);
		}
	}
	va_end(ap);
	report[reportLen] = 0;
	return 0;
}

/* the number after "label" in the report, 0 when it is not there */
static unsigned long reported(const char *label)
{
	const char *p = strstr(report, label);
	unsigned long v = 0;
	if (!p) return 0;
	p += strlen(label);
	while (*p == ' ' || *p == ':') p++;
	while (*p >= '0' && *p <= '9') v = v * 10 + *p++ - '0';
	return v;
}

int coremark_main(void);

unsigned long tCoreMark(void)
{
	unsigned long iterations, usec, ips100;
	reportLen = 0; report[0] = 0;
	coremark_main();
	iterations = reported("Iterations       ");
	usec = reported("Total ticks      ");
	g_checkIsVerdict = 1;
	if (usec) {
		/* iterations a second, to the hundredth, without floating point */
		ips100 = (unsigned long)(((unsigned long long)iterations * 100000000ULL + usec / 2) / usec);
		extraNum("it/s=", ips100 / 100);
		{ int n = strlen(g_extra); g_extra[n++] = '.'; g_extra[n++] = '0' + ips100 / 10 % 10; g_extra[n++] = '0' + ips100 % 10; g_extra[n] = 0; }
		extraNum("iterations=", iterations);
	}
	return strstr(report, "Correct operation validated") != 0;
}
