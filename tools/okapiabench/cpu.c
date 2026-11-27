/*
 *  cpu.c - OkapiaBench's tests of the processor alone
 *
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: MIT
 *
 *  Portable C: the same source gives the same checks on a 68k and on a
 *  PowerPC, except cpu_codegen, which writes each processor's own code.
 *  Compiled for the 68020 (CMakeLists.txt), as compiled code of the 1990s was.
 */

#include <string.h>
#include <OSUtils.h>

#include "bench.h"

unsigned long tSieve(void)
{
	static unsigned char flags[8192];
	long iter, i, k, count = 0;
	for (iter = 0; iter < 6000L * SCALE; iter++) {
		count = 0;
		for (i = 0; i < 8192; i++) flags[i] = 1;
		for (i = 2; i < 8192; i++)
			if (flags[i]) { for (k = i + i; k < 8192; k += i) flags[k] = 0; count++; }
	}
	return count;
}

static long sortBuf[20000];
static void quick(long *a, long lo, long hi)
{
	while (lo < hi) {
		long p = a[(lo + hi) >> 1], i = lo, j = hi, t;
		while (i <= j) {
			while (a[i] < p) i++;
			while (a[j] > p) j--;
			if (i <= j) { t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
		}
		if (j - lo < hi - i) { quick(a, lo, j); lo = i; } else { quick(a, i, hi); hi = j; }
	}
}
unsigned long tSort(void)
{
	long iter, i; unsigned long h = 0;
	for (iter = 0; iter < 150L * SCALE; iter++) {
		for (i = 0; i < 20000; i++) sortBuf[i] = rnd();
		quick(sortBuf, 0, 19999);
		h = hashBytes(h, (unsigned char *)sortBuf, 256);
	}
	return h;
}

unsigned long tCopy(void)
{
	long iter, i; unsigned long *s, *d;
	for (iter = 0; iter < 12000L * SCALE; iter++) {
		s = (unsigned long *)bufA; d = (unsigned long *)bufB;
		for (i = 0; i < BUF / 4; i++) *d++ = *s++;
	}
	return hashBytes(2166136261UL, bufB, BUF);
}

unsigned long tCrc(void)
{
	static unsigned long table[256];
	unsigned long c = 0; long i, k, iter;
	for (i = 0; i < 256; i++) {
		c = i;
		for (k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320UL ^ (c >> 1) : c >> 1;
		table[i] = c;
	}
	for (iter = 0; iter < 1250L * SCALE; iter++) {
		c = 0xffffffffUL;
		for (i = 0; i < BUF; i++) c = table[(c ^ bufA[i]) & 255] ^ (c >> 8);
	}
	return ~c;
}

/* A small LZ77: literal runs and (offset, length) copies, as archive tools of the time did */
static long lzPack(const unsigned char *src, long n, unsigned char *dst)
{
	static short head[4096];
	long i = 0, o = 0, k;
	for (k = 0; k < 4096; k++) head[k] = -1;
	while (i < n) {
		long best = 0, pos = 0;
		if (i + 3 <= n) {
			unsigned h = ((src[i] << 8) ^ (src[i + 1] << 4) ^ src[i + 2]) & 4095;
			long cand = head[h];
			if (cand >= 0) {
				cand += (i & ~32767L);
				if (cand >= i) cand -= 32768L;
				if (cand >= 0 && i - cand < 4096) {
					while (best < 18 && i + best < n && src[cand + best] == src[i + best]) best++;
					pos = i - cand;
				}
			}
			head[h] = (short)(i & 32767);
		}
		if (best >= 3) { dst[o++] = 0x80 | (best - 3) << 3 | (pos >> 8); dst[o++] = pos & 255; i += best; }
		else {
			long run = 1;
			while (run < 127 && i + run < n) run++;		/* greedy: literals to the next chance */
			if (run > 8) run = 8;
			dst[o++] = run; memcpy(dst + o, src + i, run); o += run; i += run;
		}
	}
	return o;
}
static long lzUnpack(const unsigned char *src, long n, unsigned char *dst)
{
	long i = 0, o = 0;
	while (i < n) {
		unsigned c = src[i++];
		if (c & 0x80) {
			long len = ((c >> 3) & 15) + 3, pos = ((c & 7) << 8) | src[i++];
			const unsigned char *p = dst + o - pos;
			while (len--) dst[o++] = *p++;
		} else {
			while (c--) dst[o++] = src[i++];
		}
	}
	return o;
}
long packedLen;
unsigned long tUnpack(void)
{
	long iter, n = 0;
	for (iter = 0; iter < 2000L * SCALE; iter++)
		n = lzUnpack(bufC, packedLen, bufB);
	return hashBytes(n, bufB, n);
}

void cpuPrepare(void)
{
	long i;
	rngState = 99;
	/* Data that compresses as text does: words drawn from a small vocabulary */
	for (i = 0; i < BUF; ) {
		static const char *words[] = { "window ", "the ", "Macintosh ", "of ", "QuickDraw ", "and ", "memory ", "handle ", "a ", "resource " };
		const char *w = words[rnd() % 10];
		while (*w && i < BUF) bufA[i++] = *w++;
	}
	packedLen = lzPack(bufA, BUF, bufC);
}

/* ---- calls, records and strings, in the spirit of Dhrystone ---- */

typedef struct Record {
	struct Record *next;
	long kind, value;
	char text[32];
} Record;

static Record recs[64];

static __attribute__((noinline)) long proc1(Record *r, long v)
{
	Record copy = *r;			/* a structure assigned whole */
	copy.value = v + r->kind;
	if (copy.next) copy.next->value += copy.value & 7;
	*r = copy;
	return copy.value;
}

static __attribute__((noinline)) int proc2(const char *a, const char *b)
{
	char tmp[32];
	strcpy(tmp, a);
	tmp[3] = b[(unsigned char)a[1] & 15];
	return strcmp(tmp, b);
}

unsigned long tCalls(void)
{
	long iter, i, sum = 0;
	static const char *texts[] = { "DHRYSTONE PROGRAM, SOME STRING", "DHRYSTONE PROGRAM, 1'ST STRING",
		"DHRYSTONE PROGRAM, 2'ND STRING", "DHRYSTONE PROGRAM, 3'RD STRING" };
	for (i = 0; i < 64; i++) {
		recs[i].next = &recs[(i * 37 + 11) & 63];
		recs[i].kind = i & 3; recs[i].value = i;
		strcpy(recs[i].text, texts[i & 3]);
	}
	for (iter = 0; iter < 4800000L * SCALE; iter++) {
		Record *r = &recs[iter & 63];
		sum += proc1(r, iter);
		sum += proc2(r->text, texts[(iter >> 6) & 3]) < 0 ? 1 : 2;
		switch (r->kind) {
		case 0: r->kind = 2; break;
		case 1: r->kind = (sum & 1) ? 3 : 0; break;
		case 2: r->kind = 1; break;
		default: r->kind = 0;
		}
	}
	return hashBytes((unsigned long)sum, (const unsigned char *)&recs[0].value, sizeof(long));
}

/* ---- 32-bit multiplication, division and remainder ---- */

unsigned long tMulDiv(void)
{
	long iter, a = 123456789, b = 7, q = 0;
	unsigned long u = 2166136261UL, h = 0;
	for (iter = 0; iter < 18000000L * SCALE; iter++) {
		a = (long)((unsigned long)a * 1103515245UL + 12345UL);	/* unsigned: no overflow to undefine */
		b = (long)(((unsigned long)b * 69069UL + 1) | 1);
		q = (long)((unsigned long)q + (unsigned long)(a / ((b & 0xffff) | 1) + a % 1000003L));
		u = u * 16777619UL ^ (unsigned long)a / (((unsigned long)b >> 8) | 1);
		h ^= (unsigned long)q * (unsigned long)b;
	}
	return h ^ u ^ (unsigned long)q;
}

/* ---- recursion: the towers of Hanoi and the eight queens ---- */

static long moves;
static void hanoi(int n, int from, int to, int via)
{
	if (n == 0) return;
	hanoi(n - 1, from, via, to);
	moves += from * 3 + to;
	hanoi(n - 1, via, to, from);
}

static int cols[12], diag1[24], diag2[24];
static long solutions;
static void queens(int row, int n)
{
	int c;
	if (row == n) { solutions++; return; }
	for (c = 0; c < n; c++)
		if (!cols[c] && !diag1[row + c] && !diag2[row - c + n]) {
			cols[c] = diag1[row + c] = diag2[row - c + n] = 1;
			queens(row + 1, n);
			cols[c] = diag1[row + c] = diag2[row - c + n] = 0;
		}
}

unsigned long tRecurse(void)
{
	long iter;
	moves = 0; solutions = 0;
	for (iter = 0; iter < 1440L * SCALE; iter++) {
		hanoi(16, 0, 2, 1);
		queens(0, 8);
	}
	return (unsigned long)moves ^ (unsigned long)solutions << 20;
}

/* ---- a bytecode interpreter: code that branches as a script engine does ---- */

enum { OP_PUSH, OP_LOAD, OP_STORE, OP_ADD, OP_SUB, OP_MUL, OP_AND, OP_XOR, OP_SHR, OP_JNZ, OP_DEC, OP_DUP, OP_HALT };

static const unsigned char program[] = {
	OP_PUSH, 200, OP_STORE, 0,		/* counter = 200 */
	OP_PUSH, 1, OP_STORE, 1,		/* acc = 1 */
	/* 8: loop */
	OP_LOAD, 1, OP_PUSH, 33, OP_MUL, OP_LOAD, 0, OP_XOR, OP_PUSH, 255, OP_AND,
	OP_LOAD, 1, OP_PUSH, 3, OP_SHR, OP_ADD, OP_STORE, 1,
	OP_LOAD, 0, OP_DEC, OP_DUP, OP_STORE, 0, OP_JNZ, 8,
	OP_LOAD, 1, OP_HALT
};

static long interpret(const unsigned char *pc0)
{
	long stack[16], vars[4] = { 0, 0, 0, 0 };
	long *sp = stack;
	const unsigned char *pc = pc0;
	for (;;) {
		switch (*pc++) {
		case OP_PUSH: *sp++ = *pc++; break;
		case OP_LOAD: *sp++ = vars[*pc++]; break;
		case OP_STORE: vars[*pc++] = *--sp; break;
		case OP_ADD: sp--; sp[-1] += sp[0]; break;
		case OP_SUB: sp--; sp[-1] -= sp[0]; break;
		case OP_MUL: sp--; sp[-1] *= sp[0]; break;
		case OP_AND: sp--; sp[-1] &= sp[0]; break;
		case OP_XOR: sp--; sp[-1] ^= sp[0]; break;
		case OP_SHR: sp--; sp[-1] = (unsigned long)sp[-1] >> sp[0]; break;
		case OP_JNZ: if (*--sp) pc = pc0 + *pc; else pc++; break;
		case OP_DEC: sp[-1]--; break;
		case OP_DUP: sp[0] = sp[-1]; sp++; break;
		default: return sp[-1];
		}
	}
}

unsigned long tInterp(void)
{
	long iter; unsigned long h = 0;
	for (iter = 0; iter < 32000L * SCALE; iter++)
		h = h * 31 + (unsigned long)interpret(program);
	return h;
}

/* ---- memory: a chase through a permuted list, and a stream ---- */

static unsigned long chase(long bytes, long hops)
{
	long n = bytes / sizeof(unsigned long), i, k;
	unsigned long *next = (unsigned long *)NewPtr(bytes), p = 0, h = 0;
	if (!next) { strcpy(g_extra, "nomem"); return 0; }
	for (i = 0; i < n; i++) next[i] = i;
	for (i = n - 1; i > 0; i--) {		/* Sattolo: one cycle through every entry */
		unsigned long t;
		k = (long)(((rnd() << 8) ^ rnd()) % (unsigned long)i);
		t = next[i]; next[i] = next[k]; next[k] = t;
	}
	for (k = 0; k < hops; k++) { p = next[p]; h += p; }
	DisposePtr((Ptr)next);
	return h;
}
unsigned long tChase16k(void) { return chase(16384L, 40000000L * SCALE); }
unsigned long tChase256k(void) { return chase(262144L, 40000000L * SCALE); }
unsigned long tChase4m(void) { return chase(4194304L, 40000000L * SCALE); }

unsigned long tStream(void)
{
	long n = 2097152L / sizeof(unsigned long), i, pass;
	unsigned long *a = (unsigned long *)NewPtr(2097152L), *b = (unsigned long *)NewPtr(2097152L), h;
	if (!a || !b) { if (a) DisposePtr((Ptr)a); if (b) DisposePtr((Ptr)b); strcpy(g_extra, "nomem"); return 0; }
	for (i = 0; i < n; i++) a[i] = i * 2654435761UL;
	for (pass = 0; pass < 360L * SCALE; pass++) {
		memcpy(b, a, n * sizeof *a);			/* copy */
		for (i = 0; i < n; i++) a[i] = b[i] + 3 * a[i];	/* scale and add */
	}
	h = hashBytes(2166136261UL, (const unsigned char *)a, 4096);
	DisposePtr((Ptr)a); DisposePtr((Ptr)b);
	return h;
}

/* ---- code written, its cache flushed, then run: what a translator pays ---- */

#if defined(__ppc__)
pascal void MakeDataExecutable(void *ptr, unsigned long size);	/* InterfaceLib */

unsigned long tCodegen(void)
{
	/* addis r4,0,hi ; ori r4,r4,lo ; add r3,r3,r4 ; blr, called through a
	   transition vector of our own: the code needs no TOC */
	/* static, and a barrier before each call: GCC does not see that the
	   indirect call reads the descriptor, and would drop the store of its
	   code address otherwise */
	static struct { void *code, *toc; } tv;
	unsigned long *code = (unsigned long *)NewPtr(64);
	long (*f)(long), iter, acc = 0;
	if (!code) return 0;
	__asm__ ("mr %0,2" : "=r" (tv.toc));
	tv.code = code;
	f = (long (*)(long))&tv;
	for (iter = 0; iter < 300000L * SCALE; iter++) {
		unsigned long k = iter * 2654435761UL;
		code[0] = 0x3C800000UL | (k >> 16);
		code[1] = 0x60840000UL | (k & 0xffff);
		code[2] = 0x7C632214UL;
		code[3] = 0x4E800020UL;
		MakeDataExecutable(code, 16);
		__asm__ volatile ("" ::: "memory");
		acc = f(acc);
	}
	DisposePtr((Ptr)code);
	return (unsigned long)acc;
}
#else
unsigned long tCodegen(void)
{
	/* move.l #k,d0 ; add.l 4(sp),d0 ; rts */
	unsigned short *code = (unsigned short *)NewPtr(64);
	long (*f)(long), iter, acc = 0;
	if (!code) return 0;
	f = (long (*)(long))code;
	for (iter = 0; iter < 300000L * SCALE; iter++) {
		unsigned long k = iter * 2654435761UL;
		code[0] = 0x203C; code[1] = k >> 16; code[2] = k & 0xffff;
		code[3] = 0xD0AF; code[4] = 0x0004;
		code[5] = 0x4E75;
		FlushCodeCache();
		acc = f(acc);
	}
	DisposePtr((Ptr)code);
	return (unsigned long)acc;
}
#endif
