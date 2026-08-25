/*
 * config.h — hand-written replacement for the autoconf-generated header.
 *
 * Basilisk II expects autoconf to describe the host. There is no configure run
 * for a bare-metal AArch64 target, so this states what circle-stdlib actually
 * provides. Every line here is a claim the compiler will check.
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

#define PACKAGE_NAME      "Okapia"
#define PACKAGE_VERSION   "0.1"
#define VERSION           "0.1"

#endif
