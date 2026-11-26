/*
 * config.h — what upstream's configure would say to slirp on a desktop Unix.
 *
 * slirp/slirp.h includes "config.h", which in this tree is the Pi's
 * (src/common/config.h, newlib). The desktop build puts this directory first
 * on slirp's own include path, and only there.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define HAVE_STDINT_H 1
#define HAVE_UNISTD_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_SIGNAL_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_SYS_IOCTL_H 1
#ifdef __APPLE__
#define HAVE_SYS_FILIO_H 1
#endif
#define HAVE_MEMMOVE 1
#define HAVE_INET_ATON 1
#define HAVE_STRERROR 1
#define HAVE_STRDUP 1
#define HAVE_INDEX 1
#define HAVE_GETHOSTID 1
#define HAVE_READV 1
#define HAVE_SETENV 1
#define HAVE_SOCKETPAIR 1
#define HAVE_GRANTPT 1
#define HAVE_REVOKE 1
#define TIME_WITH_SYS_TIME 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_CHAR_P 8
