/*
 *  core_portme.h - CoreMark's configuration for a Macintosh (68k and PowerPC)
 *
 *  Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)
 *  Copyright (C) 2026  Jonathan Pepin
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Adapted from CoreMark's barebones/core_portme.h (external/coremark): the
 *  one part of CoreMark a port writes. CoreMark's own files are compiled as
 *  they are, so that a result may be reported as CoreMark under EEMBC's
 *  rules (external/coremark/README.md): the run calibrates itself to at least
 *  ten seconds (ITERATIONS 0), the 2K performance parameters, one context.
 *  Modified: time from the Time Manager, ee_printf into OkapiaBench's
 *  results, data in a static block, main renamed coremark_main by the build.
 */
#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>

#define HAS_FLOAT 0		/* integer seconds: the score is computed from the ticks */
#define HAS_TIME_H 0
#define USE_CLOCK 0
#define HAS_STDIO 0
#define HAS_PRINTF 0

#ifndef COMPILER_VERSION
#define COMPILER_VERSION "GCC" __VERSION__
#endif
#ifndef FLAGS_STR
#define FLAGS_STR "-O2"
#endif
#define COMPILER_FLAGS FLAGS_STR
#define MEM_LOCATION "STATIC"

typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef double ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef ee_u32 ee_ptr_int;
typedef size_t ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x)-1) & ~3))

#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS;

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_STATIC
#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0
#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S { ee_u8 portable_id; } core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#define PERFORMANCE_RUN 1	/* TOTAL_DATA_SIZE stays 2000 */

int ee_printf(const char *fmt, ...);

#endif
