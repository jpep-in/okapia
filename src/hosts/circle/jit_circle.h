/*
 * jit_circle.h — what uae_cpu_2026 expects its platform to declare.
 *
 * In rcarmo/macemu-jit these come from its own include/main.h and
 * include/timer.h. Okapia builds that core against kanjitalk755's headers —
 * only the core changes — so the handful of names the core asks for are
 * declared here, and this header is put in front of every file of the 68k
 * engine by the Makefile (-include, OKAPIA_JIT=1 only). It declares; it
 * includes nothing but the integer types.
 *
 * Copyright (C) 2026  Jonathan Pepin
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_JIT_CIRCLE_H
#define OKAPIA_JIT_CIRCLE_H

#ifdef __cplusplus
#include <stdint.h>

// main_okapia.cpp: the pending interrupts, read and cleared in one step.
extern uint32_t ConsumeInterruptFlags (void);
// uae_cpu_2026/basilisk_glue.cpp defines it; its header is the one not taken.
extern bool UseDeferredInterruptModel (void);

// jit_circle.cpp: the seam, reached from the JIT's dispatch loop.
extern void TimerPoll (void);
// jit_circle.cpp: there is no tick thread to hand anything back to.
extern void TimerRestoreAsyncOwnership (void);
// jit_circle.cpp: microseconds since boot.
extern uint64_t GetTicks_usec (void);

// jit_circle.cpp, called by the kernel before InitAll().
extern void JitPrefsApply (void);
#endif

#endif
