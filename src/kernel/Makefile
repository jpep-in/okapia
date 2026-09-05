#
# Makefile — Okapia kernel: Circle + an emulator core + the platform layer.
#
include ../../external/circle-stdlib/Config.mk

# Which emulator this kernel carries.
#
# One engine per image, and that is not a matter of taste: the two cores define
# the same symbols — InitAll, ExitAll, EmulOp, PatchROM, Execute68k — so they
# cannot be linked together, and even the files they share compile differently
# under -DSHEEPSHAVER (planification.md §19.3). What changes with the engine is
# the upstream tree, the config.h that describes this target to it, the flags,
# the object directory, and the mangled names the linker is asked to wrap.
#
# sheepshaver names paths and nothing more for now: its core compiles for this
# target unchanged (scripts/check-engine.sh) but it has no platform layer yet,
# so there is nothing to link. Phase 20 fills that in.
ENGINE ?= basilisk

GEN     = ../../build/generated
OKAPIA  = ../../src/circle
CIRCLEH = $(CIRCLEHOME)

ifeq ($(ENGINE),basilisk)
  MACEMU         = ../../external/macemu/BasiliskII/src
  ENGINE_CONFIG  = $(OKAPIA)
  ENGINE_DEFINES = -DDIRECT_ADDRESSING -DFPU_UAE
  ENGINE_VPATH   =
else ifeq ($(ENGINE),sheepshaver)
  MACEMU         = ../../external/macemu/SheepShaver/src
  # Its own config.h, ahead of Basilisk's on the include path: upstream includes
  # the file by that plain name from both trees.
  ENGINE_CONFIG  = $(OKAPIA)/sheepshaver
  # Direct addressing comes from NATMEM_OFFSET in that config.h, and there is no
  # UAE FPU on this side — the PowerPC brings its own. HAVE_CONFIG_H is
  # autoconf's own convention, and vm_alloc.cpp is the one file that reads the
  # config through it rather than through sysdeps.h.
  ENGINE_DEFINES = -DHAVE_CONFIG_H -DSHEEPSHAVER
  ENGINE_VPATH   = $(OKAPIA)/sheepshaver:
else
  $(error ENGINE must be basilisk or sheepshaver, not "$(ENGINE)")
endif

# One directory per engine. Objects carry the engine's flags and its config.h,
# so a directory shared between the two would link a kernel built half one way
# and half the other — and succeed.
OBJ = emu-$(ENGINE)

# bincue.cpp needs SDL and is an upstream option, off by default.
CORE_SRCS   = $(filter-out $(MACEMU)/bincue.cpp,$(wildcard $(MACEMU)/*.cpp))

ifeq ($(ENGINE),basilisk)
CPU_SRCS    = $(wildcard $(MACEMU)/uae_cpu_2021/*.cpp) \
              $(MACEMU)/uae_cpu_2021/fpu/fpu_uae.cpp \
              $(MACEMU)/CrossPlatform/video_blit.cpp
GEN_SRCS    = $(GEN)/cpudefs.cpp $(GEN)/cpustbl.cpp $(GEN)/cpufunctbl.cpp
else
# The interpreted list, exactly as configure.ac:1580-1587 and :1705 build it
# when the host has no JIT backend — which AArch64 has not.
KPX         = $(MACEMU)/kpx_cpu
CPU_SRCS    = $(KPX)/src/mathlib/ieeefp.cpp $(KPX)/src/mathlib/mathlib.cpp \
              $(KPX)/src/cpu/ppc/ppc-cpu.cpp $(KPX)/src/cpu/ppc/ppc-decode.cpp \
              $(KPX)/src/cpu/ppc/ppc-execute.cpp $(KPX)/src/cpu/ppc/ppc-translate.cpp \
              $(KPX)/src/utils/utils-cpuinfo.cpp $(KPX)/sheepshaver_glue.cpp \
              $(MACEMU)/CrossPlatform/vm_alloc.cpp
GEN_SRCS    =
endif
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
ifeq ($(ENGINE),basilisk)
PLATFORM_SRCS = $(filter-out $(TRACE_SRCS),$(wildcard $(OKAPIA)/*.cpp))
ifeq ($(OKAPIA_TRACE),1)
PLATFORM_SRCS += $(TRACE_SRCS)
endif
else
# Only what this engine actually has. The shared platform files are the ones
# whose upstream header is a symbolic link between the two trees — prefs, xpram,
# timer, adb — and the rest is under sheepshaver/. Globbing src/circle would
# drag in the other engine's video and main.
PLATFORM_SRCS = $(OKAPIA)/hal_circle.cpp \
                $(OKAPIA)/prefs_circle.cpp $(OKAPIA)/xpram_circle.cpp \
                $(OKAPIA)/timer_circle.cpp $(OKAPIA)/tick_circle.cpp \
                $(OKAPIA)/input_circle.cpp $(OKAPIA)/audio_circle.cpp \
                $(OKAPIA)/user_strings_circle.cpp $(OKAPIA)/extfs_sync_circle.cpp \
                $(OKAPIA)/hfs_volume_circle.cpp \
                $(wildcard $(OKAPIA)/sheepshaver/*.cpp)
endif
# Upstream Unix files reused unchanged.
REUSED_SRCS = $(MACEMU)/Unix/extfs_unix.cpp $(MACEMU)/Unix/sys_unix.cpp

# The boot firmware: portable drawing code plus the two files that know about
# Circle — the window itself, and the bridge from USB reports to events. Its
# font and icon sources are generated from assets/, like the key table.
FIRMWARE      = ../../src/firmware
FIRMWARE_SRCS = $(wildcard $(FIRMWARE)/*.cpp) \
                $(FIRMWARE)/circle/okapia_firmware.cpp $(FIRMWARE)/circle/okapia_input.cpp \
                $(FIRMWARE)/circle/okapia_output.cpp
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

HFS_OBJS = $(addprefix $(OBJ)/hfs_,$(notdir $(HFS_SRCS:.c=.o)))

EMU_OBJS = $(addprefix $(OBJ)/,$(notdir $(EMU_SRCS:.cpp=.o))) \
           $(OBJ)/okapia_font_data.o $(OBJ)/okapia_icons_data.o $(OBJ)/okapia_strings_data.o \
           $(if $(GEN_SRCS),$(addprefix $(OBJ)/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8)))

# Our entry point is okapia_main*.cpp, not main.cpp: VPATH also finds the core's
# main.cpp, and identical names silently resolve to the wrong file.
ifeq ($(ENGINE),basilisk)
OBJS = okapia_main.o kernel.o $(EMU_OBJS) $(HFS_OBJS)
else
OBJS = okapia_main_ppc.o kernel_ppc.o $(EMU_OBJS) $(HFS_OBJS)
endif

include $(CIRCLEHOME)/Rules.mk

# No RTC on a Pi and no NTP yet, so the clock starts at zero. Stamping 1904
# dates into a volume makes its MDB inconsistent; the build time is a
# plausible floor until phase 10 provides a real source.
OKAPIA_BUILD_TIME := $(shell date +%s)
EMU_DEFINES = $(ENGINE_DEFINES) -D_GNU_SOURCE -DOKAPIA_BUILD_TIME=$(OKAPIA_BUILD_TIME)
# The engine's own config.h comes first, which is the whole reason it lives in
# a directory of its own.
EMU_INCLUDES = -I$(ENGINE_CONFIG) -I$(OKAPIA) -I$(OKAPIA)/compat -I$(GEN) -I$(HFS) \
               -I$(FIRMWARE) -I$(FIRMWARE)/circle -I$(OKAPIA)/circle \
               -I$(MACEMU)/include -I$(MACEMU)/uae_cpu_2021 -I$(MACEMU)/Unix \
               -I$(MACEMU)/CrossPlatform -I$(MACEMU)/kpx_cpu/include -I$(MACEMU)/kpx_cpu/src

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
# M68kRegisters*) to hear the Mac write its PRAM and restart itself
# (emul_op_hook_circle.cpp).
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

# The engine's own directory goes on the path only for that engine, and ahead
# of the shared one. Both halves matter: sheepshaver/main_circle.cpp has the
# same name as the other engine's on purpose — it is the same role — and VPATH
# resolves a name to the first directory that has it. Ahead, and a SheepShaver
# kernel would compile Basilisk's platform; unconditionally, and the Basilisk
# kernel compiles SheepShaver's. Both were seen, in that order.
VPATH = $(FIRMWARE):$(FIRMWARE)/circle:$(ENGINE_VPATH)$(OKAPIA):$(OKAPIA)/compat:$(MACEMU)/Unix:$(MACEMU):$(MACEMU)/uae_cpu_2021:$(MACEMU)/uae_cpu_2021/fpu:$(MACEMU)/CrossPlatform:$(MACEMU)/dummy:$(MACEMU)/kpx_cpu:$(MACEMU)/kpx_cpu/src/mathlib:$(MACEMU)/kpx_cpu/src/cpu/ppc:$(MACEMU)/kpx_cpu/src/utils:$(GEN)

# The USB HID -> ADB key table, derived from Basilisk's own keycodes file rather
# than typed out: SDL2 scancodes are HID usage IDs, so its "sdl cocoa" section
# already is the table. Regenerated whenever that file or the generator changes.
KEYCODES_H = $(GEN)/keycodes_default.h
$(KEYCODES_H): $(MACEMU)/Unix/keycodes ../../scripts/gen-keycodes.py
	@mkdir -p $(GEN)
	@../../scripts/gen-keycodes.py $< $@

$(OBJ)/input_circle.o: $(KEYCODES_H)

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

# The interface's text, typed once in a file anyone can diff. The identifiers
# come out of the same rows, so a key renamed there stops the build here.
STR_SRC = $(GEN)/okapia_strings_data.cpp
STR_IDS = $(GEN)/okapia_strings_ids.h
$(STR_SRC) $(STR_IDS): ../../scripts/gen-strings.py ../../assets/strings.tsv
	@mkdir -p $(GEN)
	@../../scripts/gen-strings.py $(STR_SRC) $(STR_IDS) ../../assets/strings.tsv

$(OBJ)/okapia_font_data.o: $(FONT_SRC) | $(OBJ)
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(OBJ)/okapia_icons_data.o: $(ICON_SRC) | $(OBJ)
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(OBJ)/okapia_strings_data.o: $(STR_SRC) $(STR_IDS) | $(OBJ)
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(OBJ):
	@mkdir -p $(OBJ)

# libhfs is plain C. It wants its own include path and none of the C++ flags.
$(OBJ)/hfs_%.o: $(HFS)/%.c | $(OBJ)
	@printf '  CC    libhfs/%s\n' "$(notdir $<)"
	@$(CC) $(CFLAGS) -I$(HFS) -MMD -MP -Wno-unused-result -c -o $@ $<

$(OBJ)/%.o: %.cpp | $(OBJ)
	@printf '  CC    %s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(OBJ)/cpuemu%.o: $(GEN)/cpuemu.cpp | $(OBJ)
	@printf '  CC    cpuemu part %s\n' "$*"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -DPART_$* -MMD -MP -c -o $@ $<

# Both engines' objects, not just the one being built: a flag change that
# matters to one usually matters to the other, and a stale directory left
# behind is the trap AGENTS.md describes.
okapia-clean:
	rm -rf emu emu-basilisk emu-sheepshaver

.PHONY: okapia-clean

# Circle works out DEPS and then never includes it, and its own compile rule
# carries no -MMD at all — so an object built by it tracks no header. That is
# not a small thing: it is how a change to a struct in one header leaves the
# other objects on the old layout, and the link succeeds. Ours generate their
# own, and this is where they are read back.
-include $(DEPS)
-include $(wildcard $(OBJ)/*.d)
