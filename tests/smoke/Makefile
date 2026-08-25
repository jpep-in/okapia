#
# Makefile — Okapia phase 1 smoke test
#
include ../../external/circle-stdlib/Config.mk

# 8 bpp: the indexed mode Mac OS uses, and the one whose palette we must exercise.
DEFINE += -DDEPTH=8

OBJS = main.o kernel.o

include $(CIRCLEHOME)/Rules.mk

INCLUDE := $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)
LIBS := $(CIRCLE_STDLIB_LIBS)

-include $(DEPS)
