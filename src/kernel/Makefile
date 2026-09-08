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
  # FPU_IEEE, not FPU_UAE, and the difference is 11 bits of mantissa and four
  # thousand decades of exponent. The UAE core keeps a 68881 register in a
  # double (uae_cpu_2021/fpu/types.h:67), so every extended value the Macintosh
  # stores and reads back comes home rounded; the C99 core keeps it in the
  # host's widest IEEE type, and on AArch64 that is binary128 — a 15-bit
  # exponent with the Mac's own bias, 112 fraction bits for the Mac's 63.
  # Nothing is lost. See patches/macemu/0008.
  ENGINE_DEFINES = -DDIRECT_ADDRESSING -DFPU_IEEE
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
  # PPC_CHECK_TICKS: interpreted instructions between two calls to
  # powerpc_check_ticks() (patches/macemu/0004). It is the only moment this
  # engine has in which it may touch the card, so it is not optional here.
  # 50000 is the figure the WebAssembly port settled on for the same
  # interpreter; sheepshaver/cpu_ticks_circle.cpp logs the rate it actually
  # produces on this board, which is what should decide it.
  ENGINE_DEFINES = -DHAVE_CONFIG_H -DSHEEPSHAVER -DPPC_CHECK_TICKS=50000
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
              $(MACEMU)/uae_cpu_2021/fpu/fpu_ieee.cpp \
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
              $(OKAPIA)/trace_traps_circle.cpp $(OKAPIA)/trace_fpu_circle.cpp
ifeq ($(ENGINE),basilisk)
PLATFORM_SRCS = $(filter-out $(TRACE_SRCS) $(SHARED_SRCS),$(wildcard $(OKAPIA)/*.cpp))
ifeq ($(OKAPIA_TRACE),1)
PLATFORM_SRCS += $(TRACE_SRCS)
endif
else
# Only what this engine actually has. The shared platform files are the ones
# whose upstream header is a symbolic link between the two trees — prefs, xpram,
# timer, adb — and the rest is under sheepshaver/. Globbing src/circle would
# drag in the other engine's video and main.
PLATFORM_SRCS = $(OKAPIA)/compositor_circle.cpp $(OKAPIA)/video_shared_circle.cpp \
                $(OKAPIA)/prefs_circle.cpp $(OKAPIA)/xpram_circle.cpp \
                $(OKAPIA)/timer_circle.cpp $(OKAPIA)/tick_circle.cpp \
                $(OKAPIA)/input_circle.cpp $(OKAPIA)/audio_circle.cpp \
                $(OKAPIA)/user_strings_circle.cpp $(OKAPIA)/extfs_sync_circle.cpp \
                $(OKAPIA)/hfs_volume_circle.cpp $(OKAPIA)/mac_encoding_circle.cpp \
                $(MACEMU)/CrossPlatform/video_blit.cpp \
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

# What must exist exactly once in the image, whichever engine is running.
#
# The image carries both emulators and the build renames one of them wholesale,
# so anything duplicated would be duplicated *state* as well: a second board
# would re-register a periodic timer handler and re-claim the mouse, and Circle
# gives back neither (AGENTS.md); a second firmware would keep its own idea of
# what the chooser answered; a second frame buffer claim leaks 4.8 MB a round.
# So the board, the entry point and the whole firmware are compiled once, into
# a directory that is not per-engine, and both halves bind to that one copy.
#
# Everything else is duplicated on purpose. It costs image size and no
# correctness: each engine's platform layer talks only to its own core.
SHARED_SRCS = $(OKAPIA)/hal_circle.cpp $(OKAPIA)/mac_ram_circle.cpp \
              okapia_boot.cpp $(FIRMWARE_SRCS)
SHARED_OBJ  = obj-shared
SHARED_OBJS = $(addprefix $(SHARED_OBJ)/,$(notdir $(SHARED_SRCS:.cpp=.o))) \
              $(SHARED_OBJ)/okapia_font_data.o $(SHARED_OBJ)/okapia_icons_data.o \
              $(SHARED_OBJ)/okapia_strings_data.o
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
           $(COMPAT_SRCS) $(PLATFORM_SRCS) $(REUSED_SRCS)

HFS_OBJS = $(addprefix $(OBJ)/hfs_,$(notdir $(HFS_SRCS:.c=.o)))

EMU_OBJS = $(addprefix $(OBJ)/,$(notdir $(EMU_SRCS:.cpp=.o))) \
           $(if $(GEN_SRCS),$(addprefix $(OBJ)/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8)))

# Our entry point is okapia_main*.cpp, not main.cpp: VPATH also finds the core's
# main.cpp, and identical names silently resolve to the wrong file.
ifeq ($(ENGINE),basilisk)
OBJS = $(OBJ)/okapia_main.o $(OBJ)/kernel.o $(EMU_OBJS) $(HFS_OBJS)
else
OBJS = $(OBJ)/okapia_main_ppc.o $(OBJ)/kernel_ppc.o $(EMU_OBJS) $(HFS_OBJS)
endif

# The prefix every symbol of the PowerPC half is given, so that the two cores
# can share an image. Used by the merge rule and by the --wrap flags below.
RENAME_PREFIX = ppc__

# -O2, and -O3 has been tried and reverted. Twice worth reading before trying
# it again, because it looks free and is not:
#
#   Speed. Under QEMU, System 7.1 to the Finder: -O2 reaches idle in 11946 and
#   12017 ms at 2988, 2745, 2970 k opcodes/s; -O3 reaches idle in 11933 and
#   12024 — the same — at 2739, 2667, 2905, so two to eight per cent *slower*,
#   twice in a row at every checkpoint, for 175 KB more image. That verdict is
#   QEMU's and QEMU is the wrong instrument for it, so it settles nothing.
#
#   Correctness. This one does settle it. -O3 vectorises GfxBlit()'s copy
#   (okapia_gfx.cpp:113) into wide NEON accesses at whatever offset the damage
#   rectangle starts at, and the destination is the output frame buffer, which
#   Circle maps as **Device** memory — translationtable64.cpp:145 gives
#   ATTRINDX_DEVICE to everything at or above the ARM's share of RAM, and the
#   GPU's frame buffer is exactly there. Device memory requires every access to
#   be naturally aligned, so the first unaligned vector store is an alignment
#   fault. Measured: the boot menu draws and the board is dead about a second
#   later, EC 0x25, DFSC 0x21, on a write to 0x3C3xxxxx.
#
# So -O3 is not merely unprofitable here, it is unsafe, and it would stay unsafe
# until every path that writes the frame buffer is proved alignment-safe.
OPTIMIZE ?= -O2

include $(CIRCLEHOME)/Rules.mk

# Rules.mk declares "all" first; ours is the merged image.
.DEFAULT_GOAL := okapia

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
# They go on each engine's **partial** link, not on the final one, and that is
# not a detail: `ld -r` resolves every reference inside the set it is given, so
# by the time the final link runs there is nothing left to redirect. AGENTS.md
# already said only calls crossing a translation unit are wrapped — collapsing
# 76 of them into one object makes that nearly none. The hooks linked, exported
# their symbols, and were never called: the Macintosh's restart and shutdown
# went unheard, so a restart from the Finder became a hard reset with no boot
# menu, and Shut Down never closed the disk image.
# The two file-name converters are replaced outright rather than patched into
# external/: extfs_unix.cpp's are identity functions, right on a UTF-8 desktop
# and wrong between a FAT card and a Macintosh (mac_encoding_circle.cpp).
ENGINE_WRAPS = --wrap=_Z11extfs_writeiPvm --wrap=_Z6EmulOptP13M68kRegisters \
               --wrap=_Z25host_encoding_to_macromanPKc \
               --wrap=_Z25macroman_to_host_encodingPKc

# Diagnostics, off by default: build with OKAPIA_TRACE=1 to hook upstream
# functions at link time rather than patching external/ (AGENTS.md). Circle
# invokes ld directly, so these are bare --wrap, not -Wl,--wrap. The symbols are
# Sys_read(void*, void*, loff_t, size_t) and Exception(int, uaecptr), mangled.
ifeq ($(OKAPIA_TRACE),1)
ENGINE_WRAPS += --wrap=_Z8Sys_readPvS_lm --wrap=_Z9Sys_writePvS_lm --wrap=_Z9Exceptionij \
                --wrap=_Z9op_illg_1j --wrap=_Z16fpuop_arithmeticjj
# One compare per guest memory access, naming the guest address of a stray one
# before the host fault it causes (patches/macemu/0007). Far too hot to leave on.
ENGINE_DEFINES += -DDIRECT_ADDRESSING_GUARD=1
# The extended round trip, checked before the Macintosh starts: it is the one
# property of the FPU decidable without a guest, and it is the one that fails
# when the register is too narrow (patches/macemu/0008).
ENGINE_DEFINES += -DFPU_SELFTEST=1
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

# MacRoman, derived from Python's own mac_roman codec — which carries Apple's
# ROMAN.TXT — rather than transcribed. See gen-macroman.py.
MACROMAN_H = $(GEN)/macroman_table.h
$(MACROMAN_H): ../../scripts/gen-macroman.py
	@mkdir -p $(GEN)
	@../../scripts/gen-macroman.py $@

$(OBJ)/mac_encoding_circle.o: $(MACROMAN_H)

# The faces and the system-folder icons, converted at build time from assets/.
# Explicit rules rather than the pattern above: a pattern cannot match a file
# that does not exist yet, and on a fresh clone these do not.
FONT_SRC = $(GEN)/okapia_font_data.cpp
ICON_SRC = $(GEN)/okapia_icons_data.cpp

$(FONT_SRC): ../../scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	@../../scripts/gen-font.py $@ $(BDF)

$(ICON_SRC): ../../scripts/gen-icons.py $(ICONDIR)/system-icons.c \
             $(ICONDIR)/cdrom-icon.c
	@mkdir -p $(GEN)
	@../../scripts/gen-icons.py $@ $(ICONDIR)/system-icons.c $(ICONDIR)/cdrom-icon.c

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

$(SHARED_OBJ):
	@mkdir -p $(SHARED_OBJ)

# The shared half is compiled once, under whichever engine's flags happen to be
# current. That is not sloppiness: these files name no emulator header, and the
# objects come out identical either way — checked by comparing them between the
# two builds, which is how the list was drawn in the first place.
$(SHARED_OBJ)/%.o: %.cpp | $(SHARED_OBJ)
	@printf '  CC    %s (shared)\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(SHARED_OBJ)/okapia_font_data.o: $(FONT_SRC) | $(SHARED_OBJ)
	@printf '  CPP   generated/%s (shared)\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(SHARED_OBJ)/okapia_icons_data.o: $(ICON_SRC) | $(SHARED_OBJ)
	@printf '  CPP   generated/%s (shared)\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $@ $<

$(SHARED_OBJ)/okapia_strings_data.o: $(STR_SRC) $(STR_IDS) | $(SHARED_OBJ)
	@printf '  CPP   generated/%s (shared)\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) $(EMU_FLAGS) -MMD -MP -c -o $(SHARED_OBJ)/okapia_strings_data.o $(STR_SRC)

$(SHARED_OBJ)/okapia_input.o: $(KEYCODES_H)

# One image, both Macintosh. The two cores export the same InitAll, ExitAll,
# PatchROM and Execute68k, so they cannot simply be linked side by side: each
# engine is partial-linked into a single object, and every symbol one of them
# *defines* is then renamed. Its undefined references — Circle, newlib, the
# shared board and firmware — are left alone, which is what makes both halves
# bind to the one copy of those.
#
# The rename list is generated at every build, so an upstream change needs
# nothing here. The two names deliberately left out of it are the engines'
# entry points (okapia_boot.h): they are the seam, and they are two functions.
# The engine is compiled against a *patched* external/macemu, and the patches
# live in the submodule's working tree (scripts/apply-patches.sh applies them
# with `git apply`, never a commit). So a `git submodule update`, a reset or a
# fresh clone removes them silently, and what follows is not a failure anyone
# can read: HasIdleTime() stops existing, so run-test.sh can no longer tell a
# finished boot from a hung one; the 68881 register goes back to a double, so
# the FPU is wrong and every boot still green; the PowerPC interpreter loses
# its seam altogether. apply-patches.sh --check was written for exactly this
# and nothing called it.
#
# Silent when the patches are in — a guard that speaks on every build is a
# guard people learn to skip — and it prints the script's own report when they
# are not. The fix it names is one command.
.PHONY: patches-check
patches-check:
	@out=$$(../../scripts/apply-patches.sh --check 2>&1) 	 || { printf '%s\n' "$$out" >&2; exit 1; }

.PHONY: okapia objects shared merge
objects: $(OBJS)
shared:  $(SHARED_OBJS)

# The check goes here and not on `objects`: okapia is the default goal and it
# re-enters make four times, so anywhere deeper would run it four times over.
okapia: patches-check
	@$(MAKE) --no-print-directory ENGINE=basilisk    objects
	@$(MAKE) --no-print-directory ENGINE=sheepshaver objects
	@$(MAKE) --no-print-directory shared
	@$(MAKE) --no-print-directory merge

merge: $(SHARED_OBJS)
	@printf '  LD -r  emu-basilisk\n'
	@$(LD) -r $(ENGINE_WRAPS) -o engine-68k.o emu-basilisk/*.o
	@printf '  LD -r  emu-sheepshaver\n'
	@$(LD) -r $(ENGINE_WRAPS) -o engine-ppc.o emu-sheepshaver/*.o
	@printf '  RENAME engine-ppc.o\n'
	@{ $(PREFIX)nm --defined-only --extern-only engine-ppc.o | awk '$$3 != "" { print $$3 }'; \
	   $(PREFIX)nm -u engine-ppc.o | awk '$$2 ~ /^__real_/ { print $$2 }'; } \
	  | sort -u | grep -vx 'OkapiaRunPowerPC' \
	  | awk -v p='$(RENAME_PREFIX)' '{ \
	        if ($$1 ~ /^__wrap_/)      { r = "__wrap_" p substr($$1, 8) } \
	        else if ($$1 ~ /^__real_/) { r = "__real_" p substr($$1, 8) } \
	        else                       { r = p $$1 } \
	        print $$1 " " r }' > engine-ppc.rename
	@$(PREFIX)objcopy --redefine-syms=engine-ppc.rename engine-ppc.o engine-ppc-renamed.o
	@printf '  LD     kernel8.elf (%s symbols renamed)\n' "$$(wc -l < engine-ppc.rename | tr -d ' ')"
	@$(LD) -o kernel8.elf -Map kernel8.map $(LDFLAGS) \
		-T $(CIRCLEHOME)/circle.ld $(CRTBEGIN) \
		engine-68k.o engine-ppc-renamed.o $(SHARED_OBJS) \
		--start-group $(LIBS) $(EXTRALIBS) --end-group $(CRTEND)
	@$(PREFIX)objcopy kernel8.elf -O binary kernel8.img
	@printf '  IMG    kernel8.img  %s KB\n' "$$(( $$(wc -c < kernel8.img) / 1024 ))"

# Both engines' objects, not just the one being built: a flag change that
# matters to one usually matters to the other, and a stale directory left
# behind is the trap AGENTS.md describes.
okapia-clean:
	rm -rf emu emu-basilisk emu-sheepshaver $(SHARED_OBJ)
	rm -f kernel8.elf kernel8.img kernel8.lst kernel8.map \
	      engine-68k.o engine-ppc.o engine-ppc-renamed.o engine-ppc.rename

.PHONY: okapia-clean

# Circle works out DEPS and then never includes it, and its own compile rule
# carries no -MMD at all — so an object built by it tracks no header. That is
# not a small thing: it is how a change to a struct in one header leaves the
# other objects on the old layout, and the link succeeds. Ours generate their
# own, and this is where they are read back.
-include $(DEPS)
-include $(wildcard $(OBJ)/*.d)
-include $(wildcard $(SHARED_OBJ)/*.d)
