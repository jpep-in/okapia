#
# Makefile — Okapia kernel: Circle + the Basilisk II core + the platform layer.
#
include ../../external/circle-stdlib/Config.mk

MACEMU  = ../../external/macemu/BasiliskII/src
GEN     = ../../build/generated
OKAPIA  = ../../src/circle
CIRCLEH = $(CIRCLEHOME)

# bincue.cpp needs SDL and is an upstream option, off by default.
CORE_SRCS   = $(filter-out $(MACEMU)/bincue.cpp,$(wildcard $(MACEMU)/*.cpp))
CPU_SRCS    = $(wildcard $(MACEMU)/uae_cpu_2021/*.cpp) \
              $(MACEMU)/uae_cpu_2021/fpu/fpu_uae.cpp \
              $(MACEMU)/CrossPlatform/video_blit.cpp
GEN_SRCS    = $(GEN)/cpudefs.cpp $(GEN)/cpustbl.cpp $(GEN)/cpufunctbl.cpp
# audio_dummy.cpp is replaced by src/circle/audio_circle.cpp; the others stay.
DUMMY_SRCS  = $(filter-out $(MACEMU)/dummy/audio_dummy.cpp,\
                           $(wildcard $(MACEMU)/dummy/*.cpp))
COMPAT_SRCS = $(wildcard $(OKAPIA)/compat/*.cpp)
# The trace wrappers reference __real_* symbols the linker only creates when the
# matching --wrap is passed, so they come in and out together with the LDFLAGS
# block below.
TRACE_SRCS  = $(OKAPIA)/trace_disk_circle.cpp $(OKAPIA)/exception_trace.cpp \
              $(OKAPIA)/trace_traps_circle.cpp
PLATFORM_SRCS = $(filter-out $(TRACE_SRCS),$(wildcard $(OKAPIA)/*.cpp))
ifeq ($(OKAPIA_TRACE),1)
PLATFORM_SRCS += $(TRACE_SRCS)
endif
# Upstream Unix files reused unchanged.
REUSED_SRCS = $(MACEMU)/Unix/extfs_unix.cpp $(MACEMU)/Unix/sys_unix.cpp

EMU_SRCS = $(CORE_SRCS) $(CPU_SRCS) $(GEN_SRCS) $(DUMMY_SRCS) \
           $(COMPAT_SRCS) $(PLATFORM_SRCS) $(REUSED_SRCS)

EMU_OBJS = $(addprefix emu/,$(notdir $(EMU_SRCS:.cpp=.o))) \
           $(addprefix emu/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8))

# Our entry point is okapia_main.cpp, not main.cpp: VPATH also finds the core's
# main.cpp, and identical names silently resolve to the wrong file.
OBJS = okapia_main.o kernel.o $(EMU_OBJS)

include $(CIRCLEHOME)/Rules.mk

EMU_DEFINES = -DDIRECT_ADDRESSING -DFPU_UAE -D_GNU_SOURCE
EMU_INCLUDES = -I$(OKAPIA) -I$(OKAPIA)/compat -I$(GEN) \
               -I$(MACEMU)/include -I$(MACEMU)/uae_cpu_2021 -I$(MACEMU)/Unix \
               -I$(MACEMU)/CrossPlatform

INCLUDE := $(EMU_INCLUDES) $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# kernel.cpp includes the core's headers, and cpu_emulation.h takes a different
# path without DIRECT_ADDRESSING — so these apply to the kernel too, not just to
# the emulator objects.
DEFINE += $(EMU_DEFINES)
# Circle's sound classes live in their own library, which circle-stdlib does
# not put in the default link line. It depends on libcircle, so it comes first.
LIBS := $(CIRCLEHOME)/lib/sound/libsound.a $(CIRCLE_STDLIB_LIBS)

# Diagnostics, off by default: build with OKAPIA_TRACE=1 to hook upstream
# functions at link time rather than patching external/ (AGENTS.md). Circle
# invokes ld directly, so these are bare --wrap, not -Wl,--wrap. The symbols are
# Sys_read(void*, void*, loff_t, size_t) and Exception(int, uaecptr), mangled.
ifeq ($(OKAPIA_TRACE),1)
LDFLAGS += --wrap=_Z8Sys_readPvS_lm --wrap=_Z9Sys_writePvS_lm --wrap=_Z9Exceptionij \
           --wrap=_Z9op_illg_1j
endif
# Toggling OKAPIA_TRACE does not invalidate kernel8.img on its own:
# rm kernel8.img kernel8.elf first, or you will run the previous kernel.

# The core throws C++ exceptions for 68k faults; Circle builds with
# -fno-exceptions, and later flags win.
EMU_FLAGS = $(EMU_DEFINES) -fexceptions -Wno-narrowing -Wno-write-strings

VPATH = $(OKAPIA):$(OKAPIA)/compat:$(MACEMU)/Unix:$(MACEMU):$(MACEMU)/uae_cpu_2021:$(MACEMU)/uae_cpu_2021/fpu:$(MACEMU)/CrossPlatform:$(MACEMU)/dummy:$(GEN)

emu:
	@mkdir -p emu

emu/%.o: %.cpp | emu
	@printf '  CC    %s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -c -o $@ $<

emu/cpuemu%.o: $(GEN)/cpuemu.cpp | emu
	@printf '  CC    cpuemu part %s\n' "$*"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -DPART_$* -c -o $@ $<

okapia-clean:
	rm -rf emu

.PHONY: okapia-clean

-include $(DEPS)
