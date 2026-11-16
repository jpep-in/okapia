/*
 *  fpu.c - MacBench's floating-point tests, for a Macintosh with a 68881,
 *          a 68882 or a 68040
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Compiled for the 68020 and the 68881 (CMakeLists.txt), so that the
 *  compiler emits the coprocessor's own instructions rather than SANE calls;
 *  the transcendental functions are written as the instructions themselves.
 *  macbench.c calls these only when Gestalt reports an FPU.
 *
 *  The work is in the spirit of Speedometer's three FPU tests - a Fourier
 *  transform, a Whetstone, a matrix product - plus the transcendental
 *  functions alone and extended-precision values going to and from memory.
 *  Sized to last about a second each on an Apple M4 running Basilisk II with
 *  its translator, so tens of seconds interpreted on a Raspberry Pi 4.
 *  Each returns a hash of the bits it computed, so two machines that agree
 *  on a check computed the same numbers: an exact FPU gives the same check as
 *  a 68881 whatever the host, a double-precision one does not.
 */

#include "fpu.h"

#ifndef SCALE
#define SCALE 1
#endif

typedef long double extended;	/* the 68881's 96-bit memory format */

#define FPU1(op, x) ({ extended r_; __asm__ (op " %1,%0" : "=f" (r_) : "f" ((extended) (x))); r_; })
#define fsin(x)   FPU1("fsin.x", x)
#define fcos(x)   FPU1("fcos.x", x)
#define ftan(x)   FPU1("ftan.x", x)
#define fatan(x)  FPU1("fatan.x", x)
#define fetox(x)  FPU1("fetox.x", x)
#define flogn(x)  FPU1("flogn.x", x)
#define fsqrt(x)  FPU1("fsqrt.x", x)
#define fint(x)   FPU1("fint.x", x)

static unsigned long hash(unsigned long h, const void *p, long n)
{
	const unsigned char *b = (const unsigned char *)p;
	while (n--) h = (h ^ *b++) * 16777619UL;
	return h;
}

static unsigned long hashX(unsigned long h, extended x) { return hash(h, &x, sizeof x); }

/* ---- a radix-2 Fourier transform of 1024 points in double ---- */

#define FFT_N 1024
#define FFT_LOG 10
static double re[FFT_N], im[FFT_N], cosT[FFT_N / 2], sinT[FFT_N / 2];

static void fft(int inverse)
{
	int i, j, k, len, half, step;
	/* bit reversal */
	for (i = 1, j = 0; i < FFT_N; i++) {
		int bit = FFT_N >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) {
			double t = re[i]; re[i] = re[j]; re[j] = t;
			t = im[i]; im[i] = im[j]; im[j] = t;
		}
	}
	for (len = 2; len <= FFT_N; len <<= 1) {
		half = len >> 1;
		step = FFT_N / len;
		for (i = 0; i < FFT_N; i += len)
			for (k = 0; k < half; k++) {
				double wr = cosT[k * step], wi = inverse ? -sinT[k * step] : sinT[k * step];
				double xr = re[i + k + half], xi = im[i + k + half];
				double tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
				re[i + k + half] = re[i + k] - tr; im[i + k + half] = im[i + k] - ti;
				re[i + k] += tr; im[i + k] += ti;
			}
	}
	if (inverse)
		for (i = 0; i < FFT_N; i++) { re[i] /= FFT_N; im[i] /= FFT_N; }
}

unsigned long tFpuFft(void)
{
	long pass, i;
	unsigned long h = 2166136261UL;
	for (i = 0; i < FFT_N / 2; i++) {
		extended a = (extended)i * (2 * 3.14159265358979323846L / FFT_N);
		cosT[i] = fcos(a);
		sinT[i] = fsin(a);
	}
	for (i = 0; i < FFT_N; i++) { re[i] = (double)((i * 37) % 101) - 50.0; im[i] = 0; }
	for (pass = 0; pass < 200L * SCALE; pass++) { fft(0); fft(1); }
	h = hash(h, re, sizeof re);
	return hash(h, im, sizeof im);
}

/* ---- a Whetstone, in extended, after Curnow and Wichmann's modules ---- */

static extended e1[4];

static void pa(extended *e, extended t, extended t2)
{
	int j;
	for (j = 0; j < 6; j++) {
		e[0] = (e[0] + e[1] + e[2] - e[3]) * t;
		e[1] = (e[0] + e[1] - e[2] + e[3]) * t;
		e[2] = (e[0] - e[1] + e[2] + e[3]) * t;
		e[3] = (-e[0] + e[1] + e[2] + e[3]) / t2;
	}
}

unsigned long tFpuWhet(void)
{
	const extended t = 0.499975L, t1 = 0.50025L, t2 = 2.0L;
	long loop, i;
	extended x, y, z, x1, x2, x3, x4;
	unsigned long h = 2166136261UL;
	for (loop = 0; loop < 3000L * SCALE; loop++) {
		/* module 1: simple identifiers */
		x1 = 1.0L; x2 = x3 = x4 = -1.0L;
		for (i = 0; i < 120; i++) {
			x1 = (x1 + x2 + x3 - x4) * t;
			x2 = (x1 + x2 - x3 + x4) * t;
			x3 = (x1 - x2 + x3 + x4) * t;
			x4 = (-x1 + x2 + x3 + x4) * t;
		}
		/* module 2: array elements */
		e1[0] = 1.0L; e1[1] = e1[2] = e1[3] = -1.0L;
		for (i = 0; i < 140; i++) {
			e1[0] = (e1[0] + e1[1] + e1[2] - e1[3]) * t;
			e1[1] = (e1[0] + e1[1] - e1[2] + e1[3]) * t;
			e1[2] = (e1[0] - e1[1] + e1[2] + e1[3]) * t;
			e1[3] = (-e1[0] + e1[1] + e1[2] + e1[3]) * t;
		}
		/* module 3: array as a parameter */
		for (i = 0; i < 120; i++) pa(e1, t, t2);
		/* module 7: trigonometric functions */
		x = y = 0.5L;
		for (i = 0; i < 32; i++) {
			x = t * fatan(t2 * fsin(x) * fcos(x) / (fcos(x + y) + fcos(x - y) - 1.0L));
			y = t * fatan(t2 * fsin(y) * fcos(y) / (fcos(x + y) + fcos(x - y) - 1.0L));
		}
		/* module 11: standard functions */
		x = 0.75L;
		for (i = 0; i < 90; i++) x = fsqrt(fetox(flogn(x) / t1));
		/* module 8: procedure calls, in extended */
		z = 1.0L;
		for (i = 0; i < 200; i++) { x = y = 1.0L; z = (t * (x + y) + z) * t / t2; }
		h = hashX(h, x1); h = hashX(h, x4); h = hashX(h, e1[3]);
		h = hashX(h, x); h = hashX(h, y); h = hashX(h, z);
	}
	return h;
}

/* ---- a 64 by 64 matrix product in double ---- */

#define MAT 64
static double ma[MAT][MAT], mb[MAT][MAT], mc[MAT][MAT];

unsigned long tFpuMatrix(void)
{
	long pass, i, j, k;
	for (i = 0; i < MAT; i++)
		for (j = 0; j < MAT; j++) {
			ma[i][j] = (double)((i * 7 + j * 3) % 17) / 8.0 - 1.0;
			mb[i][j] = (double)((i * 5 + j * 11) % 13) / 4.0 - 1.5;
		}
	for (pass = 0; pass < 50L * SCALE; pass++) {
		for (i = 0; i < MAT; i++)
			for (j = 0; j < MAT; j++) {
				double s = 0;
				for (k = 0; k < MAT; k++) s += ma[i][k] * mb[k][j];
				mc[i][j] = s;
			}
		/* feed the product back, scaled, so that each pass has new numbers */
		for (i = 0; i < MAT; i++)
			for (j = 0; j < MAT; j++) ma[i][j] = mc[i][j] / 64.0;
	}
	return hash(2166136261UL, mc, sizeof mc);
}

/* ---- the transcendental functions alone ---- */

unsigned long tFpuTrig(void)
{
	long i;
	extended x, acc = 0;
	unsigned long h = 2166136261UL;
	for (i = 0; i < 300000L * SCALE; i++) {
		x = (extended)(i % 2000) / 1000.0L - 1.0L;	/* -1 to 1 */
		acc += fsin(x) + fcos(x) + ftan(x * 0.75L) + fatan(x * 3.0L)
		     + fetox(x * 4.0L) + flogn(x + 2.0L) + fsqrt(x + 1.0L);
		if ((i & 255) == 0) h = hashX(h, acc);
	}
	return hashX(h, acc);
}

/* ---- extended values to and from memory, and to and from integers ---- */

static extended xs[512];
static long ls[512];

unsigned long tFpuMove(void)
{
	long pass, i;
	extended s = 0;
	for (i = 0; i < 512; i++) { xs[i] = (extended)(i - 256) / 3.0L; ls[i] = i * 1000 - 256000; }
	for (pass = 0; pass < 8000L * SCALE; pass++) {
		for (i = 0; i < 512; i++) {
			xs[i] = xs[i] * 1.0009765625L + (extended)ls[i] * 0.001L;	/* .X in and out, .L in */
			ls[i] = (long)fint(xs[i] * 7.0L);				/* .L out, FINT */
			s += xs[i];
		}
	}
	return hashX(hash(2166136261UL, xs, sizeof xs), s);
}
