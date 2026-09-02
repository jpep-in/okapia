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
# audio_dummy.cpp, prefs_dummy.cpp and user_strings_dummy.cpp are replaced by
# src/circle/*_circle.cpp; the others stay. prefs_dummy looks for its file
# relative to a current directory the kernel never sets, so it never found one
# on the card, user_strings_dummy's platform table is empty — which is what
# names the shared volume — and xpram_dummy names its file relatively, so where
# the Mac's PRAM landed was an accident.
DUMMY_SRCS  = $(filter-out $(MACEMU)/dummy/audio_dummy.cpp \
                           $(MACEMU)/dummy/prefs_dummy.cpp \
                           $(MACEMU)/dummy/user_strings_dummy.cpp \
                           $(MACEMU)/dummy/xpram_dummy.cpp,\
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

# The boot firmware: portable drawing code plus the two files that know about
# Circle — the window itself, and the bridge from USB reports to events. Its
# font and icon sources are generated from assets/, like the key table.
FIRMWARE      = ../../src/firmware
FIRMWARE_SRCS = $(wildcard $(FIRMWARE)/*.cpp) \
                $(FIRMWARE)/circle/okapia_firmware.cpp $(FIRMWARE)/circle/okapia_input.cpp
FONTDIR       = ../../assets/fonts
ICONDIR       = ../../assets/icons
BDF           = $(FONTDIR)/helvR10-75.bdf $(FONTDIR)/helvB10-75.bdf \
                $(FONTDIR)/helvR12-75.bdf $(FONTDIR)/helvB12-75.bdf \
                $(FONTDIR)/helvR14-75.bdf $(FONTDIR)/helvB14-75.bdf \
                $(FONTDIR)/helvR24-75.bdf $(FONTDIR)/helvB24-75.bdf \
                $(FONTDIR)/helvR14-100.bdf $(FONTDIR)/helvB14-100.bdf \
                $(FONTDIR)/helvR24-100.bdf $(FONTDIR)/helvB24-100.bdf

# libhfs (Robert Leslie, GPLv2-or-later) reads and repairs HFS volumes. It is
# what lets Okapia scavenge a volume left dirty by an interrupted session,
# which is otherwise refused by MountVol. Its C files include no system headers
# of their own — everything is behind os_open/os_read/... in os.h — so os.c
# compiles unchanged against newlib.
HFS         = ../../external/hfsutils/libhfs
HFS_SRCS    = $(wildcard $(HFS)/*.c)

EMU_SRCS = $(CORE_SRCS) $(CPU_SRCS) $(GEN_SRCS) $(DUMMY_SRCS) \
           $(COMPAT_SRCS) $(PLATFORM_SRCS) $(REUSED_SRCS) $(FIRMWARE_SRCS)

HFS_OBJS = $(addprefix emu/hfs_,$(notdir $(HFS_SRCS:.c=.o)))

EMU_OBJS = $(addprefix emu/,$(notdir $(EMU_SRCS:.cpp=.o))) \
           emu/okapia_font_data.o emu/okapia_icons_data.o \
           $(addprefix emu/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8))

# Our entry point is okapia_main.cpp, not main.cpp: VPATH also finds the core's
# main.cpp, and identical names silently resolve to the wrong file.
OBJS = okapia_main.o kernel.o $(EMU_OBJS) $(HFS_OBJS)

include $(CIRCLEHOME)/Rules.mk

# No RTC on a Pi and no NTP yet, so the clock starts at zero. Stamping 1904
# dates into a volume makes its MDB inconsistent; the build time is a
# plausible floor until phase 10 provides a real source.
OKAPIA_BUILD_TIME := $(shell date +%s)
EMU_DEFINES = -DDIRECT_ADDRESSING -DFPU_UAE -D_GNU_SOURCE -DOKAPIA_BUILD_TIME=$(OKAPIA_BUILD_TIME)
EMU_INCLUDES = -I$(OKAPIA) -I$(OKAPIA)/compat -I$(GEN) -I$(HFS) \
               -I$(FIRMWARE) -I$(FIRMWARE)/circle \
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

# Two upstream hooks that are part of the product, not diagnostics, so they are
# always on. Mangled names: extfs_write(int, void*, size_t) for the shared
# folder's flush policy (extfs_sync_circle.cpp), and EmulOp(uint16,
# M68kRegisters*) to notice the Mac writing its PRAM (xpram_hook_circle.cpp).
LDFLAGS += --wrap=_Z11extfs_writeiPvm --wrap=_Z6EmulOptP13M68kRegisters

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

VPATH = $(FIRMWARE):$(FIRMWARE)/circle:$(OKAPIA):$(OKAPIA)/compat:$(MACEMU)/Unix:$(MACEMU):$(MACEMU)/uae_cpu_2021:$(MACEMU)/uae_cpu_2021/fpu:$(MACEMU)/CrossPlatform:$(MACEMU)/dummy:$(GEN)

# The USB HID -> ADB key table, derived from Basilisk's own keycodes file rather
# than typed out: SDL2 scancodes are HID usage IDs, so its "sdl cocoa" section
# already is the table. Regenerated whenever that file or the generator changes.
KEYCODES_H = $(GEN)/keycodes_default.h
$(KEYCODES_H): $(MACEMU)/Unix/keycodes ../../scripts/gen-keycodes.py
	@mkdir -p $(GEN)
	@../../scripts/gen-keycodes.py $< $@

emu/input_circle.o: $(KEYCODES_H)

# The faces and the system-folder icons, converted at build time from assets/.
# Explicit rules rather than the pattern above: a pattern cannot match a file
# that does not exist yet, and on a fresh clone these do not.
FONT_SRC = $(GEN)/okapia_font_data.cpp
ICON_SRC = $(GEN)/okapia_icons_data.cpp

$(FONT_SRC): ../../scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	@../../scripts/gen-font.py $@ $(BDF)

$(ICON_SRC): ../../scripts/gen-icons.py $(ICONDIR)/system-icons.c
	@mkdir -p $(GEN)
	@../../scripts/gen-icons.py $@ $(ICONDIR)/system-icons.c

emu/okapia_font_data.o: $(FONT_SRC) | emu
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

emu/okapia_icons_data.o: $(ICON_SRC) | emu
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

emu:
	@mkdir -p emu

# libhfs is plain C. It wants its own include path and none of the C++ flags.
emu/hfs_%.o: $(HFS)/%.c | emu
	@printf '  CC    libhfs/%s\n' "$(notdir $<)"
	@$(CC) $(CFLAGS) -I$(HFS) -MMD -MP -Wno-unused-result -c -o $@ $<

emu/%.o: %.cpp | emu
	@printf '  CC    %s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

emu/cpuemu%.o: $(GEN)/cpuemu.cpp | emu
	@printf '  CC    cpuemu part %s\n' "$*"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -DPART_$* -MMD -MP -c -o $@ $<

okapia-clean:
	rm -rf emu

.PHONY: okapia-clean

# Circle works out DEPS and then never includes it, and its own compile rule
# carries no -MMD at all — so an object built by it tracks no header. That is
# not a small thing: it is how a change to a struct in one header leaves the
# other objects on the old layout, and the link succeeds. Ours generate their
# own, and this is where they are read back.
-include $(DEPS)
-include $(wildcard emu/*.d)
