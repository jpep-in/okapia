/*
 * sys/ioctl.h — minimal stand-in for newlib, which does not ship one.
 *
 * Only ether.cpp includes it, for ioctl(fd, FIONBIO, &on) on a UDP socket — a
 * path ether_circle.cpp will not take, since it talks to CNetDevice directly.
 * Declaring it keeps the upstream file compiling unmodified; calling it fails
 * loudly rather than silently doing nothing.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_SYS_IOCTL_H
#define OKAPIA_SYS_IOCTL_H

#ifndef FIONBIO
#define FIONBIO 0x5421
#endif
#ifndef FIONREAD
#define FIONREAD 0x541B
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in src/circle/compat/ioctl.cpp: logs and returns -1. */
int ioctl (int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
