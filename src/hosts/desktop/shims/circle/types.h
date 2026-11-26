// Copyright (C) 2026  Jonathan Pepin
// SPDX-License-Identifier: GPL-3.0-or-later
// The integer names Circle gives its own code, for the desktop build.
#ifndef _circle_types_h
#define _circle_types_h
#include <stdint.h>
#include <stddef.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
// The engine's own sysdeps.h says uintptr too, and the two must agree:
// SheepShaver's is uint64_t, which Darwin spells unsigned long long.
#ifdef SHEEPSHAVER
typedef uint64_t uintptr;
#else
typedef uintptr_t uintptr;
#endif
#endif
