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

#include <Gestalt.h>

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

/* ---- every form of the FPU's instructions, by hand ----

   The tests above are C, and a compiler uses few of the 68881's forms. This
   one is written instruction by instruction so that a translator meets each
   form a Macintosh program may use: every source format (long, single,
   extended, packed, word, double, byte) from every kind of address
   ((An), (An)+, -(An), d16(An), d8(An,Xn), d16(PC), immediate, a data
   register), every operation, the transcendentals, FMOVECR, FMOVE to every
   format with both k-factors, FMOVEM with static and dynamic lists and of
   the control registers, the rounding modes and precisions FPCR selects,
   FBcc, FDBcc and FScc on all 32 predicates, with a NaN now and then for
   BSUN, and, on a 68040, its FS... and FD... forms. Everything computed goes
   to memory and into the check. */

/* The working area, at fixed offsets the assembly below names:
     0  8 doubles in        192  8 doubles out      384  64 condition bytes
    64  4 extendeds in      256  4 extendeds out    448  FPCR FPSR FPIAR saved
   112  4 singles in        304  4 singles out      464  2 extendeds for FMOVEM (An)
   128  4 longs in          320  4 longs out        496  data registers out
   144  4 words in          336  4 words out        768  FMOVEM stack (grows down)
   152  4 bytes in          344  4 bytes out
   156  3 packed in         348  3 packed out */
static unsigned char work[768];

static void put_bytes(int at, const void *p, int n) { const unsigned char *s = (const unsigned char *)p; while (n--) work[at++] = *s++; }

static void forms_inputs(long i)
{
	int j;
	for (j = 0; j < 8; j++) {
		double d = (double)((i * 13 + j * 7) % 2000 - 1000) / 127.0;
		put_bytes(j * 8, &d, 8);
	}
	for (j = 0; j < 4; j++) {
		extended x = (extended)((i * 29 + j * 11) % 3000 - 1500) / 61.0L;
		float s = (float)((i * 17 + j * 5) % 500 - 250) / 9.0f;
		long l = (i * 7919 + j * 104729) % 200001 - 100000;
		short w = (short)((i * 31 + j * 3) % 2001 - 1000);
		signed char b = (signed char)((i * 3 + j) % 201 - 100);
		put_bytes(64 + j * 12, &x, 12);
		put_bytes(112 + j * 4, &s, 4);
		put_bytes(128 + j * 4, &l, 4);
		put_bytes(144 + j * 2, &w, 2);
		work[152 + j] = (unsigned char)b;
	}
	{	/* packed: +1.25E+0, -3.5E-2, and +9.87654321E+(i % 40) */
		static const unsigned long p[9] = { 0x00000001, 0x25000000, 0, 0xC0020003, 0x50000000, 0,
		                                    0x00000009, 0x87654321, 0 };
		unsigned long q[9];
		int e = (int)(i % 40);
		for (j = 0; j < 9; j++) q[j] = p[j];
		q[6] |= (unsigned long)(((e / 10) << 4) | (e % 10)) << 16;
		put_bytes(156, q, 36);
	}
}

static void forms_once(long i)
{
	register unsigned char *w __asm__("a0") = work;
	register long n __asm__("d0") = i;
	__asm__ volatile (
		/* loads: every format, every kind of address */
		"fmoved %%a0@,%%fp0\n\t"
		"lea %%a0@(64),%%a1\n\t"
		"fmovex %%a1@+,%%fp1\n\t"
		"fmovex %%a1@+,%%fp2\n\t"
		"fmoves %%a0@(112),%%fp3\n\t"
		"movel %%d0,%%d1\n\t"
		"andl #7,%%d1\n\t"
		"fmoved %%a0@(0,%%d1:l:8),%%fp4\n\t"
		"fmovel %%a0@(128),%%fp5\n\t"
		"fmovew %%a0@(144),%%fp6\n\t"
		"fmoveb %%a0@(152),%%fp7\n\t"
		"fmovel %%d1,%%fp7\n\t"
		"lea %%a0@(144),%%a1\n\t"
		"faddw %%a1@+,%%fp6\n\t"
		"faddw %%a1@+,%%fp6\n\t"
		"lea %%a0@(124),%%a1\n\t"
		"fadds %%a1@-,%%fp3\n\t"
		"fmovep %%a0@(156),%%fp5\n\t"
		"fmulp %%a0@(168),%%fp5\n\t"
		"faddp %%a0@(180),%%fp5\n\t"
		"faddd %%pc@(9f),%%fp0\n\t"
		"faddd #0r0.5,%%fp0\n\t"
		"fsubx #0r1.25,%%fp1\n\t"
		"fmuls #0r0.75,%%fp2\n\t"
		"fsubl #3,%%fp4\n\t"
		"faddw #7,%%fp6\n\t"
		"faddb #3,%%fp7\n\t"
		"fmuld %%a0@(8),%%fp0\n\t"
		"fdivd %%a0@(16),%%fp1\n\t"
		/* operations */
		"faddx %%fp1,%%fp0\n\t"
		"fsubx %%fp2,%%fp1\n\t"
		"fmulx %%fp3,%%fp2\n\t"
		"fdivx %%fp4,%%fp3\n\t"
		"fsglmulx %%fp5,%%fp4\n\t"
		"fsgldivx %%fp6,%%fp5\n\t"
		"fmodx %%fp7,%%fp6\n\t"
		"fremx %%fp5,%%fp7\n\t"
		"fscalel #2,%%fp0\n\t"
		"fgetexpx %%fp1,%%fp4\n\t"
		"fgetmanx %%fp2,%%fp5\n\t"
		"fintx %%fp3,%%fp6\n\t"
		"fintrzx %%fp3,%%fp7\n\t"
		"fabsx %%fp0,%%fp0\n\t"
		"fnegx %%fp1,%%fp1\n\t"
		"fsqrtx %%fp0,%%fp2\n\t"
		"fmovecrx #0x00,%%fp3\n\t"
		"fmovecrx #0x0c,%%fp4\n\t"
		"fmovecrx #0x34,%%fp5\n\t"
		"fmovemx %%fp0-%%fp7,%%a0@(192+0)\n\t"
		"fmoved %%fp0,%%a0@(192)\n\t"
		/* the transcendentals, on values kept in their domains */
		"fmoved %%a0@,%%fp0\n\t"
		"fdivl #200,%%fp0\n\t"
		"fsinx %%fp0,%%fp1\n\t"
		"fcosx %%fp0,%%fp2\n\t"
		"ftanx %%fp0,%%fp3\n\t"
		"fatanx %%fp0,%%fp4\n\t"
		"fasinx %%fp0,%%fp5\n\t"
		"facosx %%fp0,%%fp6\n\t"
		"faddx %%fp1,%%fp2\n\t"
		"faddx %%fp3,%%fp4\n\t"
		"faddx %%fp5,%%fp6\n\t"
		"fmoved %%fp2,%%a0@(200)\n\t"
		"fmoved %%fp4,%%a0@(208)\n\t"
		"fmoved %%fp6,%%a0@(216)\n\t"
		"fetoxx %%fp0,%%fp1\n\t"
		"fetoxm1x %%fp0,%%fp2\n\t"
		"ftwotoxx %%fp0,%%fp3\n\t"
		"ftentoxx %%fp0,%%fp4\n\t"
		"fsinhx %%fp0,%%fp5\n\t"
		"fcoshx %%fp0,%%fp6\n\t"
		"ftanhx %%fp0,%%fp7\n\t"
		"faddx %%fp1,%%fp2\n\t"
		"faddx %%fp3,%%fp4\n\t"
		"faddx %%fp5,%%fp6\n\t"
		"faddx %%fp7,%%fp6\n\t"
		"fmoved %%fp2,%%a0@(224)\n\t"
		"fmoved %%fp4,%%a0@(232)\n\t"
		"fmoved %%fp6,%%a0@(240)\n\t"
		"fabsx %%fp0,%%fp1\n\t"
		"faddl #1,%%fp1\n\t"
		"flognx %%fp1,%%fp2\n\t"
		"flognp1x %%fp0,%%fp3\n\t"
		"flog10x %%fp1,%%fp4\n\t"
		"flog2x %%fp1,%%fp5\n\t"
		"fatanhx %%fp0,%%fp6\n\t"
		"fsincosx %%fp0,%%fp7:%%fp1\n\t"
		"faddx %%fp2,%%fp3\n\t"
		"faddx %%fp4,%%fp5\n\t"
		"faddx %%fp6,%%fp7\n\t"
		"faddx %%fp1,%%fp7\n\t"
		"fmovex %%fp3,%%a0@(256)\n\t"
		"fmovex %%fp5,%%a0@(268)\n\t"
		"fmovex %%fp7,%%a0@(280)\n\t"
		/* stores: every format, both k-factors, a data register */
		"fmoved %%a0@(8),%%fp0\n\t"
		"fmuld %%a0@(24),%%fp0\n\t"
		"fmoves %%fp0,%%a0@(304)\n\t"
		"lea %%a0@(320),%%a1\n\t"
		"fmovel %%fp0,%%a1@+\n\t"
		"fmovel %%fp1,%%a1@+\n\t"
		"fmovew %%fp0,%%a0@(336)\n\t"
		"fmoveb %%fp6,%%a0@(344)\n\t"
		"fmovep %%fp0,%%a0@(348){#5}\n\t"
		"moveq #-3,%%d2\n\t"
		"fmovep %%fp1,%%a0@(360){%%d2}\n\t"
		"fmovep %%fp3,%%a0@(372){#17}\n\t"
		"fmovel %%fp0,%%d3\n\t"
		"movel %%d3,%%a0@(496)\n\t"
		"fmovew %%fp2,%%d3\n\t"
		"movel %%d3,%%a0@(500)\n\t"
		"lea %%a0@(256),%%a1\n\t"
		"fmovex %%fp4,%%a1@-\n\t"
		"fmoved %%fp5,%%a0@(184+64)\n\t"
		/* FMOVEM: static and dynamic lists, -(An), (An)+, (An), d16(An) */
		"lea %%a0@(768),%%a1\n\t"
		"fmovemx %%fp0-%%fp7,%%a1@-\n\t"
		"fmovemx %%a1@+,%%fp0-%%fp7\n\t"
		"moveq #0x5a,%%d2\n\t"
		"fmovemx %%d2,%%a1@-\n\t"
		"fmovemx %%a1@+,%%d2\n\t"
		"fmovemx %%fp2/%%fp3,%%a0@(464)\n\t"
		"fmovemx %%a0@(464),%%fp4/%%fp5\n\t"
		"lea %%a0@(464),%%a1\n\t"
		"fmovemx %%a1@,%%fp6\n\t"
		"fmovex %%fp6,%%a0@(292)\n\t"
		/* rounding: every mode at every precision, then FPCR as it was */
		"fmoveml %%fpcr/%%fpsr,%%a0@(448)\n\t"
		"fmoved %%a0@(16),%%fp0\n\t"
		"fmoved %%a0@(24),%%fp1\n\t"
		"fmovel #0x10,%%fpcr\n\t"
		"fdivx %%fp1,%%fp0\n\t"
		"fmoved %%fp0,%%a0@(248)\n\t"
		"fmovel #0x60,%%fpcr\n\t"
		"fdivx %%fp1,%%fp0\n\t"
		"fmovex %%fp0,%%a0@(256)\n\t"
		"fmovel #0xb0,%%fpcr\n\t"
		"faddx %%fp1,%%fp0\n\t"
		"fmovex %%fp0,%%a0@(268)\n\t"
		"movel %%d0,%%d2\n\t"
		"andl #0xf0,%%d2\n\t"
		"fmovel %%d2,%%fpcr\n\t"
		"fsqrtx %%fp1,%%fp2\n\t"
		"fintx %%fp2,%%fp3\n\t"
		"fmovel %%fp2,%%a0@(332)\n\t"
		"fmovex %%fp3,%%a0@(280)\n\t"
		"fmoveml %%a0@(448),%%fpcr/%%fpsr\n\t"
		"lea %%a0@(456),%%a1\n\t"
		"fmovel %%fpiar,%%a1@\n\t"
		/* conditions: FScc on all 32 predicates, after a compare that is
		   now and then against a NaN; FBcc and FDBcc */
		"fmoved %%a0@,%%fp0\n\t"
		"fmoved %%a0@(8),%%fp1\n\t"
		"btst #2,%%d0\n\t"
		"beqs 1f\n\t"
		"fmovecrx #0x0f,%%fp1\n\t"
		"fdivx %%fp1,%%fp1\n"			/* 0/0: a quiet NaN */
		"1:\n\t"
		"fcmpx %%fp1,%%fp0\n\t"
		"lea %%a0@(384),%%a1\n\t"
		"fsf %%a1@+\n\tfseq %%a1@+\n\tfsogt %%a1@+\n\tfsoge %%a1@+\n\t"
		"fsolt %%a1@+\n\tfsole %%a1@+\n\tfsogl %%a1@+\n\tfsor %%a1@+\n\t"
		"fsun %%a1@+\n\tfsueq %%a1@+\n\tfsugt %%a1@+\n\tfsuge %%a1@+\n\t"
		"fsult %%a1@+\n\tfsule %%a1@+\n\tfsne %%a1@+\n\tfst %%a1@+\n\t"
		"fssf %%a1@+\n\tfsseq %%a1@+\n\tfsgt %%a1@+\n\tfsge %%a1@+\n\t"
		"fslt %%a1@+\n\tfsle %%a1@+\n\tfsgl %%a1@+\n\tfsgle %%a1@+\n\t"
		"fsngle %%a1@+\n\tfsngl %%a1@+\n\tfsnle %%a1@+\n\tfsnlt %%a1@+\n\t"
		"fsnge %%a1@+\n\tfsngt %%a1@+\n\tfssne %%a1@+\n\tfsst %%a1@+\n\t"
		"ftstx %%fp0\n\t"
		"fslt %%d3\n\t"
		"moveb %%d3,%%a1@+\n\t"
		"moveq #0,%%d3\n\t"
		"fbogt 2f\n\t"
		"addql #1,%%d3\n"
		"2:\n\t"
		"fbule 3f\n\t"
		"addql #2,%%d3\n"
		"3:\n\t"
		"fbgl 4f\n\t"
		"addql #4,%%d3\n"
		"4:\n\t"
		"moveb %%d3,%%a1@+\n\t"
		"fmovel #1,%%fp2\n\t"
		"fmovel #0,%%fp3\n\t"
		"moveq #20,%%d3\n"
		"5:\n\t"
		"faddx %%fp2,%%fp3\n\t"
		"fcmpl #7,%%fp3\n\t"
		"fdbge %%d3,5b\n\t"
		"moveb %%d3,%%a1@+\n\t"
		"fmovex %%fp3,%%a0@(292)\n\t"
		"fmovel %%fpsr,%%a0@(452)\n\t"
		"bras 8f\n\t"
		".even\n"
		"9: .double 0r0.0078125\n"
		"8:\n\t"
		: "+a" (w), "+d" (n)
		:
		: "a1", "d1", "d2", "d3", "fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "fp6", "fp7", "memory", "cc");
}

/* The 68040's forms that round to single (FS...) or to double (FD...)
   whatever FPCR says; a 68881 or a 68882 has none. */
static void forms_040(void)
{
	register unsigned char *w __asm__("a0") = work;
	__asm__ volatile (
		/* encoded by hand: an assembler for the 68020 refuses them */
		".short 0xf210,0x5440\n\t"		/* fsmove.d (a0),fp0 */
		".short 0xf228,0x54c4,0x0008\n\t"	/* fdmove.d 8(a0),fp1 */
		".short 0xf200,0x0462\n\t"		/* fsadd.x fp1,fp0 */
		".short 0xf228,0x54ec,0x0010\n\t"	/* fdsub.d 16(a0),fp1 */
		".short 0xf200,0x00e3\n\t"		/* fsmul.x fp0,fp1 */
		".short 0xf200,0x00e4\n\t"		/* fddiv.x fp0,fp1 */
		".short 0xf200,0x0141\n\t"		/* fssqrt.x fp0,fp2 */
		".short 0xf200,0x01c5\n\t"		/* fdsqrt.x fp0,fp3 */
		".short 0xf200,0x0658\n\t"		/* fsabs.x fp1,fp4 */
		".short 0xf200,0x06de\n\t"		/* fdneg.x fp1,fp5 */
		".short 0xf200,0x09e6\n\t"		/* fdadd.x fp2,fp3 */
		".short 0xf200,0x12e8\n\t"		/* fssub.x fp4,fp5 */
		"fmovex %%fp3,%%a0@(268)\n\t"
		"fmovex %%fp5,%%a0@(280)\n\t"
		"fmoved %%fp1,%%a0@(208)\n\t"
		: "+a" (w)
		:
		: "fp0", "fp1", "fp2", "fp3", "fp4", "fp5", "memory", "cc");
}

unsigned long tFpuForms(void)
{
	long i;
	long cpu = 0;
	unsigned long h = 2166136261UL;
	Gestalt(gestaltProcessorType, &cpu);
	for (i = 0; i < 120000L * SCALE; i++) {
		forms_inputs(i);
		forms_once(i);
		if (cpu >= gestalt68040)
			forms_040();
		h = hash(h, work + 192, 512 - 192);
	}
	return h;
}

/* ---- calls, as compiled code makes them ----

   Speedometer's own code saves and restores the FPU registers it uses at
   every call (FMOVEM to and from the stack) and passes doubles through
   memory; MacBench's other tests are too well optimised to. These functions
   are kept apart so that the compiler does the same. */

static __attribute__((noinline)) double poly(double x, double a, double b, double c)
{
	extended t = (extended)x * a + b;	/* in extended registers it must save */
	extended u = t * x + c;
	extended v = u * t - x;
	return (double)(v * u + t);
}

static __attribute__((noinline)) double step(double x, double y)
{
	extended a = poly(x, 0.5, -0.25, 0.125);
	extended b = poly(y, -0.75, 0.5, 0.0625);
	extended c = a * b + x - y;
	return (double)(c / (1.0L + a * a + b * b));
}

unsigned long tFpuCalls(void)
{
	long i;
	double x = 0.25, y = -0.5;
	unsigned long h = 2166136261UL;
	for (i = 0; i < 2000000L * SCALE; i++) {
		double z = step(x, y);
		y = x;
		x = z + (double)(i & 15) * 0.01;
		if ((i & 1023) == 0) h = hash(h, &x, sizeof x);
	}
	return hash(h, &x, sizeof x);
}
