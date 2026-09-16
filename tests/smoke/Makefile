# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile — Okapia phase 1 smoke test
#
include ../../external/circle-stdlib/Config.mk

# 8 bpp: the indexed mode Mac OS uses, and the one whose palette we must exercise.
DEFINE += -DDEPTH=8

OBJS = main.o kernel.o exctest.o

include $(CIRCLEHOME)/Rules.mk

INCLUDE := $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# Circle builds with -fno-exceptions (Rules.mk). Basilisk's core throws C++
# exceptions for 68k faults on the upstream default path, so we need to know
# whether unwinding works here. Later flags win, so this re-enables them.
CPPFLAGS += -fexceptions
LIBS := $(CIRCLE_STDLIB_LIBS)

-include $(DEPS)
