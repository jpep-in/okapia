// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// Barriers and caches: the host's compiler and kernel keep them.
#ifndef _circle_synchronize_h
#define _circle_synchronize_h
#define IRQ_LEVEL  1
#define TASK_LEVEL 0
static inline void EnterCritical (unsigned = 0) {}
static inline void LeaveCritical (void) {}
static inline void DataMemBarrier (void) { __sync_synchronize (); }
static inline void DataSyncBarrier (void) { __sync_synchronize (); }
static inline void InvalidateInstructionCache (void) {}
static inline void CleanAndInvalidateDataCacheRange (unsigned long, unsigned long) {}
#endif
