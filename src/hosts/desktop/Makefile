# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile — Okapia on a desktop (macOS first; Linux has what it needs).
#
#   make -C src/hosts/desktop            build/desktop/okapia
#   make -C src/hosts/desktop run        open the window on qemu/sd-contents
#
# The boot menu and the code that is the same on every host (src/firmware,
# src/common, the portable files of src/circle, src/kernel's CKernel), the 68k
# engine as the kernel builds it (Basilisk II's core, uae_cpu_2021, the 80-bit
# softfloat FPU), libhfs; compiled by the host's compiler behind a shim of the
# Circle classes they use (shims/), with SDL3 below. -MMD -MP on every rule, as
# everywhere in this tree (docs/contributing/build.md, Pitfalls).
#
# The 68k opcode tables are the kernel's: run scripts/gen-cpu.sh once.

ROOT     = ../../..
FIRMWARE = $(ROOT)/src/firmware
COMMON   = $(ROOT)/src/common
CIRCLE   = $(ROOT)/src/circle
MACEMU   = $(ROOT)/external/macemu/BasiliskII/src
UAE      = $(MACEMU)/uae_cpu_2021
SOFTFLOAT = $(UAE)/fpu/softfloat
LIBHFS   = $(ROOT)/external/hfsutils/libhfs
KERNEL   = $(ROOT)/src/kernel
CPUGEN   = $(ROOT)/build/generated
OUT      = $(ROOT)/build/desktop
OBJ      = $(OUT)/obj
GEN      = $(OUT)/generated

# The macOS 27.2 SDK's libSystem.tbd does not link on this machine; the 26.x
# one does (the same workaround as tests/host). An SDKROOT already set wins.
ifeq ($(shell uname),Darwin)
SDKROOT ?= $(lastword $(sort $(wildcard /Library/Developer/CommandLineTools/SDKs/MacOSX26*.sdk)))
export SDKROOT
endif

SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

# The build's version, as for the kernel (scripts/gen-version.sh).
$(shell $(ROOT)/scripts/gen-version.sh $(GEN)/okapia_version.h)

CXX      ?= clang++
CC       ?= clang
# -O2 on the Pi is for its Device memory; a desktop has none.
OPTIMIZE ?= -O3
# The engine's defines, as src/kernel/Makefile gives them (ENGINE_DEFINES).
ENGINE_DEFINES = -DDIRECT_ADDRESSING -DFPU_SOFTFLOAT -DM68K_NO_FAULT_PC \
                 -DOKAPIA_BUILD_TIME=$(shell date +%s)
CXXFLAGS = $(OPTIMIZE) -g -std=c++17 -Wall -Wextra -Wno-unused-parameter \
           -DOKAPIA_HOSTED $(ENGINE_DEFINES) -D_GNU_SOURCE -DOKAPIA_NETBENCH=0 \
           -include shims/okapia_circle.h -Ishims \
           -I. -I$(CIRCLE) -I$(COMMON) -I$(FIRMWARE) -I$(FIRMWARE)/circle -I$(KERNEL) \
           -I$(GEN) -I$(CPUGEN) -I$(LIBHFS) \
           -I$(MACEMU)/include -I$(UAE) -I$(MACEMU)/Unix -I$(MACEMU)/CrossPlatform \
           $(SDL_CFLAGS)
# The 68k→AArch64 translator (patches/macemu/0012), on by default here: an
# arm64 host has the MAP_JIT memory it wants, and the `jit` preference still
# decides whether a Macintosh uses it (docs/topics/engine-68k.md). Off on x86_64.
ifeq ($(shell uname -m),arm64)
OKAPIA_JIT_ARM64 ?= 1
endif
ifeq ($(OKAPIA_JIT_ARM64),1)
CXXFLAGS += -DJIT_ARM64 -DJIT_ARM64_OWN_POLL -DOKAPIA_TRANSLATOR=1
JIT_SRCS  = $(UAE)/jit_arm64/jit_arm64.cpp
endif
# Upstream's files, quietly: their warnings are upstream's.
UPFLAGS  = $(CXXFLAGS) -w

FONTDIR  = $(ROOT)/assets/fonts
BDF      = $(FONTDIR)/helvR10-75.bdf $(FONTDIR)/helvB10-75.bdf $(FONTDIR)/helvR12-75.bdf \
           $(FONTDIR)/helvB12-75.bdf $(FONTDIR)/helvR14-75.bdf $(FONTDIR)/helvB14-75.bdf \
           $(FONTDIR)/helvR24-75.bdf $(FONTDIR)/helvB24-75.bdf $(FONTDIR)/helvR14-100.bdf \
           $(FONTDIR)/helvB14-100.bdf $(FONTDIR)/helvR24-100.bdf $(FONTDIR)/helvB24-100.bdf
FONT_SRC = $(GEN)/okapia_font_data.cpp
ICON_SRC = $(GEN)/okapia_icons_data.cpp
STR_SRC  = $(GEN)/okapia_strings_data.cpp
STR_IDS  = $(GEN)/okapia_strings_ids.h

FIRMWARE_SRCS = $(wildcard $(FIRMWARE)/*.cpp)
COMMON_SRCS   = $(COMMON)/okapia_menu.cpp
# src/circle's files that build here unchanged. Left out: the board's own
# (hal, board_sound, board_net, wlan, clock, net_clock, net_bench, execmem,
# mac_ram, quickdraw_worker: board_desktop.cpp plays them), the sound and the
# network for now (audio_circle, ether_circle: upstream's dummies stand in),
# the JIT glue and the trace files.
PLATFORM_SRCS = $(addprefix $(CIRCLE)/,card_circle.cpp compositor_circle.cpp \
                cpu_ticks_circle.cpp emul_op_hook_circle.cpp extfs_sync_circle.cpp \
                hfs_volume_circle.cpp input_circle.cpp main_circle.cpp \
                mac_encoding_circle.cpp net_config_circle.cpp prefs_circle.cpp \
                quickdraw_blit.cpp quickdraw_circle.cpp rom_chime.cpp tick_circle.cpp \
                timer_circle.cpp user_strings_circle.cpp video_circle.cpp \
                video_shared_circle.cpp video_sizes_circle.cpp xpram_circle.cpp)
KERNEL_SRCS   = $(KERNEL)/kernel.cpp $(KERNEL)/okapia_main.cpp
# The core, as src/kernel/Makefile lists it for the 68k engine.
CORE_SRCS     = $(filter-out $(MACEMU)/bincue.cpp,$(wildcard $(MACEMU)/*.cpp))
CPU_SRCS      = $(UAE)/basilisk_glue.cpp $(UAE)/memory.cpp $(UAE)/newcpu.cpp \
                $(UAE)/readcpu.cpp $(UAE)/fpu/fpu_softfloat.cpp
GEN_CPU_SRCS  = $(CPUGEN)/cpudefs.cpp $(CPUGEN)/cpustbl.cpp $(CPUGEN)/cpufunctbl.cpp
DUMMY_SRCS    = $(filter-out $(MACEMU)/dummy/prefs_dummy.cpp \
                             $(MACEMU)/dummy/user_strings_dummy.cpp \
                             $(MACEMU)/dummy/xpram_dummy.cpp,\
                             $(wildcard $(MACEMU)/dummy/*.cpp))
REUSED_SRCS   = $(MACEMU)/Unix/sys_unix.cpp $(MACEMU)/Unix/extfs_unix.cpp \
                $(MACEMU)/CrossPlatform/video_blit.cpp
# Sparse bundles declined, as on the card: the plain-image factory takes over.
COMPAT_SRCS   = $(CIRCLE)/compat/disk_stubs.cpp
DESKTOP_SRCS  = okapia_host_desktop.cpp main_desktop.cpp board_desktop.cpp wraps_desktop.cpp
HFS_SRCS      = $(filter-out $(LIBHFS)/memcmp.c,$(wildcard $(LIBHFS)/*.c))
SF_SRCS       = $(SOFTFLOAT)/softfloat.c $(SOFTFLOAT)/softfloat_fpsp.c $(SOFTFLOAT)/softfloat_decimal.c
KEYCODES_H    = $(GEN)/keycodes_default.h
MACROMAN_H    = $(GEN)/macroman_table.h

# Prefixed by origin: upstream's prefs.cpp and ours would otherwise both want
# to be prefs.o, and the second would quietly win.
OBJS = $(addprefix $(OBJ)/fw_,$(notdir $(FIRMWARE_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/co_,$(notdir $(COMMON_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/p_,$(notdir $(PLATFORM_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/k_,$(notdir $(KERNEL_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/up_,$(notdir $(CORE_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/cpu_,$(notdir $(CPU_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/cpu_,$(notdir $(JIT_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/cg_,$(notdir $(GEN_CPU_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8)) \
       $(addprefix $(OBJ)/dm_,$(notdir $(DUMMY_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/re_,$(notdir $(REUSED_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/p_,$(notdir $(COMPAT_SRCS:.cpp=.o))) \
       $(addprefix $(OBJ)/sf_,$(notdir $(SF_SRCS:.c=.o))) \
       $(addprefix $(OBJ)/d_,$(DESKTOP_SRCS:.cpp=.o)) \
       $(addprefix $(OBJ)/hfs_,$(notdir $(HFS_SRCS:.c=.o))) \
       $(OBJ)/gen_okapia_font_data.o $(OBJ)/gen_okapia_icons_data.o \
       $(OBJ)/gen_okapia_strings_data.o

all: $(OUT)/okapia

$(OUT)/okapia: $(OBJS)
	@printf '  LD    %s\n' "$(notdir $@)"
	@$(CXX) -o $@ $^ $(SDL_LIBS)

$(OBJ):
	@mkdir -p $(OBJ)

$(FONT_SRC): $(ROOT)/scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-font.py $@ $(BDF)

$(ICON_SRC): $(ROOT)/scripts/gen-icons.py $(ROOT)/assets/icons/system-icons.c \
             $(ROOT)/assets/icons/cdrom-icon.c
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-icons.py $@ $(ROOT)/assets/icons/system-icons.c \
	    $(ROOT)/assets/icons/cdrom-icon.c

# One run writes both, and every object waits for the identifiers: a fresh tree
# has no .d file yet to say who includes them.
$(STR_SRC) $(STR_IDS) &: $(ROOT)/scripts/gen-strings.py $(ROOT)/assets/strings.tsv
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-strings.py $(STR_SRC) $(STR_IDS) $(ROOT)/assets/strings.tsv

define compile
	@printf '  CXX   %s\n' "$(notdir $<)"
	@$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<
endef

$(OBJ)/fw_%.o: $(FIRMWARE)/%.cpp | $(OBJ) $(STR_IDS)
	$(compile)
$(OBJ)/co_%.o: $(COMMON)/%.cpp | $(OBJ) $(STR_IDS)
	$(compile)
$(OBJ)/p_%.o: $(CIRCLE)/%.cpp | $(OBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
	$(compile)
$(OBJ)/p_%.o: $(CIRCLE)/compat/%.cpp | $(OBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
	$(compile)
# The hooks the Pi puts in with --wrap, which Apple's linker does not have
# (docs/notes/research/desktop-host.md): the definition is renamed in the one
# file that holds it, and wraps_desktop.cpp gives the old name to the hook.
$(OBJ)/up_emul_op.o: UPDEFS = -DEmulOp=OkapiaRealEmulOp
$(OBJ)/up_extfs.o: UPDEFS = -DPrefsFindString=OkapiaCardPrefsString
$(OBJ)/re_extfs_unix.o: UPDEFS = -Dextfs_write=OkapiaRealExtfsWrite \
    -Dhost_encoding_to_macroman=OkapiaUnixHostToMac -Dmacroman_to_host_encoding=OkapiaUnixMacToHost
$(OBJ)/re_sys_unix.o: UPDEFS = -DSys_open=OkapiaUnixSysOpen

define upcompile
	@printf '  CXX   upstream/%s\n' "$(notdir $<)"
	@$(CXX) $(UPFLAGS) $(UPDEFS) -MMD -MP -c -o $@ $<
endef

$(OBJ)/up_%.o: $(MACEMU)/%.cpp | $(OBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
	$(upcompile)
$(OBJ)/cpu_%.o: $(UAE)/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/cpu_%.o: $(UAE)/fpu/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/cpu_%.o: $(UAE)/jit_arm64/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/cg_%.o: $(CPUGEN)/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/cpuemu%.o: $(CPUGEN)/cpuemu.cpp | $(OBJ)
	@printf '  CXX   cpuemu part %s\n' "$*"
	@$(CXX) $(UPFLAGS) -DPART_$* -MMD -MP -c -o $@ $<
$(OBJ)/dm_%.o: $(MACEMU)/dummy/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/re_%.o: $(MACEMU)/Unix/%.cpp | $(OBJ) $(MACROMAN_H)
	$(upcompile)
$(OBJ)/re_%.o: $(MACEMU)/CrossPlatform/%.cpp | $(OBJ)
	$(upcompile)
$(OBJ)/k_%.o: $(KERNEL)/%.cpp | $(OBJ) $(STR_IDS)
	$(compile)
$(OBJ)/sf_%.o: $(SOFTFLOAT)/%.c | $(OBJ)
	@printf '  CC    softfloat/%s\n' "$(notdir $<)"
	@$(CC) -O2 -w -MMD -MP -c -o $@ $<

$(KEYCODES_H): $(MACEMU)/Unix/keycodes $(ROOT)/scripts/gen-keycodes.py
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-keycodes.py $< $@
$(MACROMAN_H): $(ROOT)/scripts/gen-macroman.py
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-macroman.py $@
$(OBJ)/d_%.o: %.cpp | $(OBJ) $(STR_IDS)
	$(compile)
$(OBJ)/gen_%.o: $(GEN)/%.cpp | $(OBJ) $(STR_IDS)
	$(compile)
$(OBJ)/hfs_%.o: $(LIBHFS)/%.c | $(OBJ)
	@printf '  CC    libhfs/%s\n' "$(notdir $<)"
	@$(CC) -O2 -w -I$(LIBHFS) -MMD -MP -c -o $@ $<

-include $(wildcard $(OBJ)/*.d)

run: $(OUT)/okapia
	$(OUT)/okapia $(ROOT)/qemu/sd-contents

clean:
	rm -rf $(OBJ) $(OUT)/okapia

.PHONY: all run clean
