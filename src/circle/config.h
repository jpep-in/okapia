/*
 * config.h — hand-written replacement for the autoconf-generated header.
 *
 * Basilisk II expects autoconf to describe the host. There is no configure run
 * for a bare-metal AArch64 target, so this states what circle-stdlib actually
 * provides. Every line here is a claim the compiler will check.
 *
 * With this file in place, upstream's Unix/sysdeps.h works unchanged: no
 * Okapia-specific sysdeps.h is needed. See planification.md, spike V2.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CONFIG_H
#define OKAPIA_CONFIG_H

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

#endif
