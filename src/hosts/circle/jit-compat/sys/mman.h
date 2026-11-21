/*
 * sys/mman.h — a stand-in, on the include path only with OKAPIA_JIT=1.
 *
 * uae_cpu_2026/compiler/compemu_support_arm.cpp includes <sys/mman.h>
 * unguarded on AArch64. Every use of it is behind __linux__, __APPLE__ or
 * JIT_DEBUG_MEM_CORRUPTION, none of which this build defines, so nothing here
 * is ever called: the JIT's memory comes from vm_acquire() (jit_circle.cpp).
 * Deliberately empty of functions — a call that slipped through must fail to
 * compile, not link against a stub.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_JIT_COMPAT_SYS_MMAN_H
#define OKAPIA_JIT_COMPAT_SYS_MMAN_H
#endif
