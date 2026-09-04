/*
 * config.h — what SheepShaver's autoconf would have said about this target.
 *
 * The same job src/circle/config.h does for Basilisk II, for the other engine.
 * It sits in a directory of its own because upstream includes it as plain
 * "config.h" from both trees: the build puts this directory ahead of
 * src/circle on the include path, and each engine then finds its own.
 *
 * It is very nearly the Basilisk one. Sweeping the whole SheepShaver core past
 * the AArch64 compiler on 2026-09-05 — the 23 files of Unix/Makefile.in plus
 * the six of kpx_cpu — found **no portability problem at all**: every one
 * compiles unchanged. Only two things had to be said, and both are below.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_SS_CONFIG_H
#define OKAPIA_SS_CONFIG_H

#define SIZEOF_SHORT      2
#define SIZEOF_INT        4
#define SIZEOF_LONG       8
#define SIZEOF_LONG_LONG  8
#define SIZEOF_VOID_P     8
#define SIZEOF_FLOAT      4
#define SIZEOF_DOUBLE     8
#define SIZEOF_LONG_DOUBLE 16
/* AArch64 runs little-endian here; WORDS_BIGENDIAN stays undefined. */

/* newlib headers present through circle-stdlib */
#define HAVE_UNISTD_H     1
#define HAVE_FCNTL_H      1
#define HAVE_SYS_STAT_H   1
#define HAVE_SYS_TIME_H   1
#define HAVE_SYS_TYPES_H  1
#define HAVE_STDINT_H     1
#define HAVE_INTTYPES_H   1
#define HAVE_CLOCK_GETTIME 1
#define HAVE_MEMCPY       1
#define HAVE_STRDUP       1

/* Circle's scheduler is cooperative and non-preemptive: no pthreads. */
/* #undef HAVE_PTHREADS */
/* No POSIX signals, so no SIGSEGV-based tricks and no VOSF. */
/* #undef HAVE_SIGACTION */
/* #undef ENABLE_VOSF */

/*
 * rom_patches.cpp and rsrc_patches.cpp call htons/ntohs without including a
 * network header: upstream gets them transitively from a Unix libc. Declaring
 * them is enough. Do NOT #include <arpa/inet.h> here — config.h is pulled in
 * before everything else, and dragging a system header this early breaks the
 * include order across the whole core (locale_t undefined).
 */
#ifdef __cplusplus
extern "C" {
#endif
unsigned short htons (unsigned short);
unsigned short ntohs (unsigned short);
unsigned int   htonl (unsigned int);
unsigned int   ntohl (unsigned int);

/*
 * ether.cpp uses the historical resolver API. circle-newlib ships only the
 * modern getaddrinfo, so the pieces it misses are declared here and stubbed in
 * compat/. The code path guarded by them is the UDP tunnel, which Okapia never
 * takes — but it is selected at run time, so it still has to compile.
 */
struct hostent {
    char  *h_name;
    char **h_aliases;
    int    h_addrtype;
    int    h_length;
    char **h_addr_list;
};
struct hostent *gethostbyname (const char *name);
#ifdef __cplusplus
}
#endif

#define PACKAGE_NAME      "Okapia"
#define PACKAGE_VERSION   "0.1"
#define VERSION           "0.1"

/* NOT VERSION_MAJOR / VERSION_MINOR. Basilisk's config.h states them because
   its tree reads them before version.h is seen; SheepShaver's version.h
   declares them as const ints, and a macro of the same name turns that
   declaration into a syntax error. */

/* --- What SheepShaver reads and Basilisk does not --- */

/* The PowerPC is emulated: everything the native path needs — signal stacks,
   a host instruction decoder, ppc_asm.tmpl — drops out of sysdeps.h. */
#define EMULATED_PPC 1

/* kpx_cpu's rounding modes come from <fenv.h>; without this ppc-execute.cpp
   cannot find FE_TONEAREST. */
#define HAVE_FENV_H 1

/* Direct addressing: host = NATMEM_OFFSET + guest. A placeholder value here —
   the real one is a variable set at startup, which is what planification.md
   §19.4 asks for and what the two upstream conditions provide. */
#define NATMEM_OFFSET 0x40000000UL

#endif
