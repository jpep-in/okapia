/*
 * okapia_circle.h — the bridge between Basilisk's world and Circle's.
 *
 * Platform files include both sysdeps.h (which pulls newlib headers) and Circle
 * headers. Circle's <circle/types.h> uses ASSERT_STATIC, which it expects from
 * *Circle's* assert.h — but newlib's assert.h wins the include search here, so
 * the macro is missing and every Circle header fails to parse. Defining it first
 * is enough, and avoids reordering the include paths for the whole build.
 *
 * Include this before any <circle/...> header in a platform file.
 *
 * Copyright (C) 2026  Okapia contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef OKAPIA_CIRCLE_H
#define OKAPIA_CIRCLE_H

#ifndef ASSERT_STATIC
#define ASSERT_STATIC(expr) static_assert (expr, #expr)
#endif

#include <circle/types.h>
#include <circle/logger.h>
#include <circle/memory.h>
/* Deliberately NOT <circle/new.h>: its heap-typed operator new collides with
   libstdc++'s <new> (different exception specifiers). Platform code calls
   CMemorySystem::HeapAllocate directly, which is clearer for raw blocks. */
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <circle/synchronize.h>

#endif
