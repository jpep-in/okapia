/*
 *  fp.c - OkapiaBench's floating-point tests in double, for a 68k with an
 *         FPU and for a PowerPC
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Portable C in double: a Fourier transform and a matrix product (from
 *  MacBench's fpu.c, 2026-11-27), a Whetstone and a small ray tracer. Their
 *  times compare a 68k and a PowerPC; their checks do not, since a 68881
 *  computes the intermediate results in extended and rounds twice. The
 *  PowerPC build is compiled with -ffp-contract=off, so that no multiply-add
 *  is fused either.
 */

#include <math.h>

#include "bench.h"

static unsigned long hash(unsigned long h, const void *p, long n)
{
	const unsigned char *b = (const unsigned char *)p;
	while (n--) h = (h ^ *b++) * 16777619UL;
	return h;
}

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

unsigned long tFpFft(void)
{
	long pass, i;
	unsigned long h = 2166136261UL;
	for (i = 0; i < FFT_N / 2; i++) {
		double a = (double)i * (2 * 3.14159265358979323846 / FFT_N);
		cosT[i] = cos(a);	/* the C library's: no 68881 instruction, so a PowerPC runs it too */
		sinT[i] = sin(a);
	}
	for (i = 0; i < FFT_N; i++) { re[i] = (double)((i * 37) % 101) - 50.0; im[i] = 0; }
	for (pass = 0; pass < 200L * SCALE; pass++) { fft(0); fft(1); }
	h = hash(h, re, sizeof re);
	return hash(h, im, sizeof im);
}

/* ---- a 64 by 64 matrix product in double ---- */

#define MAT 64
static double ma[MAT][MAT], mb[MAT][MAT], mc[MAT][MAT];

unsigned long tFpMatrix(void)
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

/* ---- a Whetstone in double, after Curnow and Wichmann's modules ---- */

static double e2[4];

static __attribute__((noinline)) void pa2(double *e, double t, double t2)
{
	int j;
	for (j = 0; j < 6; j++) {
		e[0] = (e[0] + e[1] + e[2] - e[3]) * t;
		e[1] = (e[0] + e[1] - e[2] + e[3]) * t;
		e[2] = (e[0] - e[1] + e[2] + e[3]) * t;
		e[3] = (-e[0] + e[1] + e[2] + e[3]) / t2;
	}
}

unsigned long tFpWhet(void)
{
	const double t = 0.499975, t1 = 0.50025, t2 = 2.0;
	long loop, i;
	double x, y, z, x1, x2, x3, x4;
	unsigned long h = 2166136261UL;
	for (loop = 0; loop < 1000L * SCALE; loop++) {
		x1 = 1.0; x2 = x3 = x4 = -1.0;
		for (i = 0; i < 120; i++) {
			x1 = (x1 + x2 + x3 - x4) * t;
			x2 = (x1 + x2 - x3 + x4) * t;
			x3 = (x1 - x2 + x3 + x4) * t;
			x4 = (-x1 + x2 + x3 + x4) * t;
		}
		e2[0] = 1.0; e2[1] = e2[2] = e2[3] = -1.0;
		for (i = 0; i < 120; i++) pa2(e2, t, t2);
		x = y = 0.5;
		for (i = 0; i < 16; i++) {
			x = t * atan(t2 * sin(x) * cos(x) / (cos(x + y) + cos(x - y) - 1.0));
			y = t * atan(t2 * sin(y) * cos(y) / (cos(x + y) + cos(x - y) - 1.0));
		}
		x = 0.75;
		for (i = 0; i < 40; i++) x = sqrt(exp(log(x) / t1));
		z = 1.0;
		for (i = 0; i < 200; i++) { x1 = y = 1.0; z = (t * (x1 + y) + z) * t / t2; }
		h = hash(h, &x4, sizeof x4); h = hash(h, &e2[3], sizeof e2[3]);
		h = hash(h, &x, sizeof x); h = hash(h, &z, sizeof z);
	}
	return h;
}

/* ---- a ray tracer: three spheres, a plane, a light and shadows, 160 by 120 ---- */

typedef struct { double x, y, z; } V3;
static V3 v3(double x, double y, double z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vmul(V3 a, double k) { return v3(a.x * k, a.y * k, a.z * k); }
static double vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vnorm(V3 a) { return vmul(a, 1.0 / sqrt(vdot(a, a))); }

static const struct { double x, y, z, r, refl; } spheres[3] = {
	{ 0.0, 1.0, 5.0, 1.0, 0.5 }, { -2.0, 0.6, 6.5, 0.6, 0.2 }, { 1.8, 0.8, 4.2, 0.8, 0.0 }
};

/* the nearest hit along o + t d: the sphere's index, 3 for the floor, -1 for none */
static int hit(V3 o, V3 d, double *tmin)
{
	int k, which = -1;
	*tmin = 1e30;
	for (k = 0; k < 3; k++) {
		V3 oc = vsub(o, v3(spheres[k].x, spheres[k].y, spheres[k].z));
		double b = vdot(oc, d), c = vdot(oc, oc) - spheres[k].r * spheres[k].r, disc = b * b - c;
		if (disc > 0) {
			double t = -b - sqrt(disc);
			if (t > 1e-4 && t < *tmin) { *tmin = t; which = k; }
		}
	}
	if (d.y < -1e-6) {
		double t = -o.y / d.y;
		if (t > 1e-4 && t < *tmin) { *tmin = t; which = 3; }
	}
	return which;
}

static double trace(V3 o, V3 d, int depth)
{
	double t, shade, tl;
	V3 p, n, l;
	int k = hit(o, d, &t);
	if (k < 0) return 0.2 + 0.3 * d.y;
	p = vadd(o, vmul(d, t));
	n = k == 3 ? v3(0, 1, 0) : vnorm(vsub(p, v3(spheres[k].x, spheres[k].y, spheres[k].z)));
	l = vnorm(vsub(v3(5, 8, -2), p));
	shade = vdot(n, l);
	if (shade < 0) shade = 0;
	if (hit(p, l, &tl) >= 0) shade *= 0.3;	/* in shadow */
	if (k == 3) shade *= ((long)floor(p.x) + (long)floor(p.z)) & 1 ? 0.5 : 1.0;	/* a checkerboard */
	if (k < 3 && spheres[k].refl > 0 && depth < 3) {
		V3 r = vsub(d, vmul(n, 2 * vdot(d, n)));
		shade = shade * (1 - spheres[k].refl) + spheres[k].refl * trace(p, r, depth + 1);
	}
	return shade;
}

unsigned long tRaytrace(void)
{
	static unsigned char img[160];
	long pass, x, y;
	unsigned long h = 2166136261UL;
	for (pass = 0; pass < 8L * SCALE; pass++)
		for (y = 0; y < 120; y++) {
			for (x = 0; x < 160; x++) {
				V3 d = vnorm(v3((x - 80) / 100.0, (60 - y) / 100.0 + 0.1, 1.0));
				double s = trace(v3(0, 1.2, -1.0 - pass * 0.25), d, 0);
				img[x] = s >= 1 ? 255 : (unsigned char)(s * 255);
			}
			h = hash(h, img, sizeof img);
		}
	return h;
}
