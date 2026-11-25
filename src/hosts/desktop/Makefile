# Copyright (C) 2026  Jonathan Pepin
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Makefile — Okapia on a desktop (macOS first; Linux has what it needs).
#
#   make -C src/hosts/desktop            build/desktop/okapia
#   make -C src/hosts/desktop run        open the window on qemu/sd-contents
#
# The boot menu and the code that is the same on every host (src/firmware,
# src/common with the shared *_okapia platform files, src/kernel's CKernel), the 68k
# engine as the kernel builds it (Basilisk II's core, uae_cpu_2021, the 80-bit
# softfloat FPU), libhfs; compiled by the host's compiler behind a shim of the
# Circle classes they use (shims/), with SDL3 below. -MMD -MP on every rule, as
# everywhere in this tree (docs/contributing/build.md, Pitfalls).
#
# The 68k opcode tables are the kernel's: run scripts/gen-cpu.sh once.

ROOT     = ../../..
FIRMWARE = $(ROOT)/src/firmware
COMMON   = $(ROOT)/src/common
# The shared platform files (src/common); the name says what they were.
CIRCLE   = $(ROOT)/src/common
CHOST    = $(ROOT)/src/hosts/circle
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
           -I. -I$(CIRCLE) -I$(COMMON) -I$(FIRMWARE) -I$(CHOST)/firmware -I$(KERNEL) \
           -I$(GEN) -I$(CPUGEN) -I$(LIBHFS) -I$(ROOT)/external/stb -isystem $(ROOT)/external/miniz \
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
# AppKit includes <Block.h>, which a case-insensitive disk finds as libhfs's
# block.h, and okapia_gfx.h's Rect meets the Mac's: the Objective-C++ file
# gets no include path of the project's.
MMFLAGS  = $(OPTIMIZE) -g -std=c++17 -Wall
# Upstream's files, quietly: their warnings are upstream's.
UPFLAGS  = $(CXXFLAGS) -w

FONTDIR  = $(ROOT)/assets/fonts
# The faces: TeX Gyre Heros, regular and bold (scripts/fetch-fonts.sh), carried
# whole as bytes and rasterised at run time (src/firmware/okapia_font_vector.cpp).
OTF      = $(FONTDIR)/texgyreheros-regular.otf $(FONTDIR)/texgyreheros-bold.otf
FONT_SRC = $(GEN)/okapia_font_data.cpp
ICON_SRC = $(GEN)/okapia_icons_data.cpp
STR_SRC  = $(GEN)/okapia_strings_data.cpp
STR_IDS  = $(GEN)/okapia_strings_ids.h

FIRMWARE_SRCS = $(wildcard $(FIRMWARE)/*.cpp)
COMMON_SRCS   = $(COMMON)/okapia_menu.cpp $(COMMON)/okapia_rom.cpp $(COMMON)/okapia_card_check.cpp
# src/common's platform files, all of which build here. Left out: the Circle host's
# own (src/hosts/circle:
# (hal, board_sound, board_net, wlan, clock, net_clock, net_bench, execmem,
# mac_ram: board_desktop.cpp plays them; board_sound's device
# and chime: sound_desktop.cpp; board_net: board_net_desktop.cpp), the JIT glue
# and the trace files.
PLATFORM_SRCS = $(addprefix $(CIRCLE)/,audio_okapia.cpp card_okapia.cpp ether_okapia.cpp compositor_okapia.cpp \
                cpu_ticks_okapia.cpp emul_op_hook_okapia.cpp extfs_sync_okapia.cpp \
                hfs_volume_okapia.cpp input_okapia.cpp main_okapia.cpp \
                mac_encoding_okapia.cpp net_config_okapia.cpp prefs_okapia.cpp \
                rom_chime.cpp tick_okapia.cpp \
                timer_okapia.cpp user_strings_okapia.cpp video_okapia.cpp \
                video_shared_okapia.cpp video_sizes_okapia.cpp xpram_okapia.cpp)
KERNEL_SRCS   = $(KERNEL)/kernel.cpp $(KERNEL)/okapia_main.cpp
# The core, as src/kernel/Makefile lists it for the 68k engine.
CORE_SRCS     = $(filter-out $(MACEMU)/bincue.cpp,$(wildcard $(MACEMU)/*.cpp))
CPU_SRCS      = $(UAE)/basilisk_glue.cpp $(UAE)/memory.cpp $(UAE)/newcpu.cpp \
                $(UAE)/readcpu.cpp $(UAE)/fpu/fpu_softfloat.cpp
GEN_CPU_SRCS  = $(CPUGEN)/cpudefs.cpp $(CPUGEN)/cpustbl.cpp $(CPUGEN)/cpufunctbl.cpp
DUMMY_SRCS    = $(filter-out $(MACEMU)/dummy/audio_dummy.cpp \
                             $(MACEMU)/dummy/ether_dummy.cpp \
                             $(MACEMU)/dummy/prefs_dummy.cpp \
                             $(MACEMU)/dummy/user_strings_dummy.cpp \
                             $(MACEMU)/dummy/xpram_dummy.cpp,\
                             $(wildcard $(MACEMU)/dummy/*.cpp))
REUSED_SRCS   = $(MACEMU)/Unix/sys_unix.cpp $(MACEMU)/Unix/extfs_unix.cpp \
                $(MACEMU)/CrossPlatform/video_blit.cpp
# Sparse bundles declined, as on the card: the plain-image factory takes over.
COMPAT_SRCS   = $(CHOST)/compat/disk_stubs.cpp
DESKTOP_SRCS  = okapia_host_desktop.cpp main_desktop.cpp board_desktop.cpp wraps_desktop.cpp \
                cards_desktop.cpp install_manifest.cpp install_card.cpp install_release.cpp \
                install_fetch.cpp install_host.cpp \
                sound_desktop.cpp board_net_desktop.cpp
ifeq ($(shell uname),Darwin)
# The application's menus, Command handed to the Macintosh (Objective-C++).
DESKTOP_MM    = menus_desktop.mm
SDL_LIBS     += -framework AppKit -framework DiskArbitration -framework CoreFoundation
endif
HFS_SRCS      = $(filter-out $(LIBHFS)/memcmp.c,$(wildcard $(LIBHFS)/*.c))
# The network: upstream's slirp (NAT in user space, no privileges), under
# ether_okapia.cpp as on the board, by way of board_net_desktop.cpp.
SLIRP_SRCS    = $(wildcard $(MACEMU)/slirp/*.c)
# miniz (MIT), the release zips' reader (install_release.cpp).
MINIZ         = $(ROOT)/external/miniz
MINIZ_SRCS    = $(MINIZ)/miniz.c $(MINIZ)/miniz_tdef.c $(MINIZ)/miniz_tinfl.c $(MINIZ)/miniz_zip.c
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
       $(addprefix $(OBJ)/sl_,$(notdir $(SLIRP_SRCS:.c=.o))) \
       $(addprefix $(OBJ)/mz_,$(notdir $(MINIZ_SRCS:.c=.o))) \
       $(addprefix $(OBJ)/d_,$(DESKTOP_SRCS:.cpp=.o)) \
       $(addprefix $(OBJ)/d_,$(DESKTOP_MM:.mm=.o)) \
       $(addprefix $(OBJ)/hfs_,$(notdir $(HFS_SRCS:.c=.o))) \
       $(OBJ)/gen_okapia_font_data.o $(OBJ)/gen_okapia_icons_data.o \
       $(OBJ)/gen_okapia_strings_data.o


# The PowerPC engine, as the kernel builds it for ENGINE=sheepshaver: SheepShaver's
# core, kpx_cpu interpreted, src/common/sheepshaver and the shared platform files
# compiled again with its config.h, kernel_ppc.cpp. Both cores define InitAll,
# PatchROM, Execute68k…: on the board every symbol the PowerPC object defines is
# renamed ppc__ (src/kernel/Makefile). Here ld -r keeps OkapiaRunPowerPC global
# and turns every other definition into a local one, which a Mach-O partial
# link does by itself (-exported_symbol): the same seam, one function wide.
SS        = $(ROOT)/external/macemu/SheepShaver/src
KPX       = $(SS)/kpx_cpu
PPCOBJ    = $(OBJ)/ppc
# C++14: kpx_cpu uses std::unary_function, gone from libc++ in C++17.
PPCFLAGS  = $(OPTIMIZE) -g -std=gnu++14 -w \
            -DOKAPIA_HOSTED -DHAVE_CONFIG_H -DSHEEPSHAVER -DPPC_CHECK_TICKS=50000 \
            -D_GNU_SOURCE -DOKAPIA_NETBENCH=0 -DOKAPIA_BUILD_TIME=$(shell date +%s) \
            -include shims/okapia_circle.h -Ishims \
            -I. -I$(CIRCLE)/sheepshaver -I$(CIRCLE) -I$(COMMON) -I$(FIRMWARE) \
            -I$(CHOST)/firmware -I$(KERNEL) -I$(GEN) -I$(CPUGEN) -I$(LIBHFS) \
            -I$(SS)/include -I$(SS)/Unix -I$(SS)/CrossPlatform \
            -I$(KPX)/include -I$(KPX)/src $(SDL_CFLAGS)
PPC_CORE  = $(filter-out $(SS)/bincue.cpp,$(wildcard $(SS)/*.cpp))
PPC_CPU   = $(KPX)/src/mathlib/ieeefp.cpp $(KPX)/src/mathlib/mathlib.cpp \
            $(KPX)/src/cpu/ppc/ppc-cpu.cpp $(KPX)/src/cpu/ppc/ppc-decode.cpp \
            $(KPX)/src/cpu/ppc/ppc-execute.cpp $(KPX)/src/cpu/ppc/ppc-translate.cpp \
            $(KPX)/src/utils/utils-cpuinfo.cpp $(KPX)/sheepshaver_glue.cpp \
            $(SS)/CrossPlatform/vm_alloc.cpp
PPC_DUMMY = $(filter-out $(SS)/dummy/audio_dummy.cpp $(SS)/dummy/ether_dummy.cpp \
                         $(SS)/dummy/prefs_dummy.cpp $(SS)/dummy/user_strings_dummy.cpp \
                         $(SS)/dummy/xpram_dummy.cpp,$(wildcard $(SS)/dummy/*.cpp))
PPC_PLAT  = $(addprefix $(CIRCLE)/,compositor_okapia.cpp video_shared_okapia.cpp \
            video_sizes_okapia.cpp prefs_okapia.cpp xpram_okapia.cpp timer_okapia.cpp \
            tick_okapia.cpp input_okapia.cpp audio_okapia.cpp user_strings_okapia.cpp \
            extfs_sync_okapia.cpp hfs_volume_okapia.cpp card_okapia.cpp \
            mac_encoding_okapia.cpp ether_okapia.cpp) \
            $(SS)/CrossPlatform/video_blit.cpp
# Named ps_ apart: main, video and cpu_ticks exist under both names, and the
# 68k engine's would win a shared pattern.
PPC_SHEEP = $(wildcard $(CIRCLE)/sheepshaver/*.cpp)
PPC_REUSED = $(SS)/Unix/extfs_unix.cpp $(SS)/Unix/sys_unix.cpp
PPC_OBJS  = $(addprefix $(PPCOBJ)/up_,$(notdir $(PPC_CORE:.cpp=.o))) \
            $(addprefix $(PPCOBJ)/cpu_,$(notdir $(PPC_CPU:.cpp=.o))) \
            $(addprefix $(PPCOBJ)/dm_,$(notdir $(PPC_DUMMY:.cpp=.o))) \
            $(addprefix $(PPCOBJ)/p_,$(notdir $(PPC_PLAT:.cpp=.o))) \
            $(addprefix $(PPCOBJ)/ps_,$(notdir $(PPC_SHEEP:.cpp=.o))) \
            $(addprefix $(PPCOBJ)/re_,$(notdir $(PPC_REUSED:.cpp=.o))) \
            $(PPCOBJ)/k_kernel_ppc.o $(PPCOBJ)/k_okapia_main_ppc.o $(PPCOBJ)/d_wraps_desktop.o

all: $(OUT)/okapia

$(OUT)/okapia: $(OBJS) $(OBJ)/engine-ppc.o
	@printf '  LD    %s\n' "$(notdir $@)"
	@$(CXX) -o $@ $^ $(SDL_LIBS) -lcurl

$(OBJ)/engine-ppc.o: $(PPC_OBJS)
	@printf '  LD -r  engine-ppc.o (OkapiaRunPowerPC alone global)\n'
	@ld -r -exported_symbol _OkapiaRunPowerPC -o $@ $^

$(PPCOBJ):
	@mkdir -p $(PPCOBJ)

$(PPCOBJ)/up_extfs.o: PPCDEFS = -DPrefsFindString=OkapiaCardPrefsString
$(PPCOBJ)/re_extfs_unix.o: PPCDEFS = -Dextfs_write=OkapiaRealExtfsWrite \
    -Dhost_encoding_to_macroman=OkapiaUnixHostToMac -Dmacroman_to_host_encoding=OkapiaUnixMacToHost
$(PPCOBJ)/re_sys_unix.o: PPCDEFS = -DSys_open=OkapiaUnixSysOpen
# vm_alloc.cpp's macOS branch (a CrashReporter fix for 10.4) wants the Mach
# headers its configure would have pulled in.
$(PPCOBJ)/cpu_vm_alloc.o: PPCDEFS = -include mach/mach.h -include sys/utsname.h
$(PPCOBJ)/up_ether.o: PPCDEFS = -DInitStreamModule=OkapiaRealInitStreamModule \
    -DTerminateStreamModule=OkapiaRealTerminateStreamModule -Dether_open=OkapiaRealEtherOpen

define ppccompile
	@printf '  CXX   ppc/%s\n' "$(notdir $<)"
	@$(CXX) $(PPCFLAGS) $(PPCDEFS) -MMD -MP -c -o $@ $<
endef
$(PPCOBJ)/up_%.o: $(SS)/%.cpp | $(PPCOBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
	$(ppccompile)
$(PPCOBJ)/cpu_%.o: $(KPX)/src/mathlib/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/cpu_%.o: $(KPX)/src/cpu/ppc/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/cpu_%.o: $(KPX)/src/utils/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/cpu_%.o: $(KPX)/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/cpu_%.o: $(SS)/CrossPlatform/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/dm_%.o: $(SS)/dummy/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/p_%.o: $(CIRCLE)/%.cpp | $(PPCOBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
	$(ppccompile)
$(PPCOBJ)/ps_%.o: $(CIRCLE)/sheepshaver/%.cpp | $(PPCOBJ) $(STR_IDS)
	$(ppccompile)
$(PPCOBJ)/p_%.o: $(SS)/CrossPlatform/%.cpp | $(PPCOBJ)
	$(ppccompile)
$(PPCOBJ)/re_%.o: $(SS)/Unix/%.cpp | $(PPCOBJ) $(MACROMAN_H)
	$(ppccompile)
$(PPCOBJ)/k_%.o: $(KERNEL)/%.cpp | $(PPCOBJ) $(STR_IDS)
	$(ppccompile)
$(PPCOBJ)/d_%.o: %.cpp | $(PPCOBJ) $(STR_IDS)
	$(ppccompile)
-include $(wildcard $(PPCOBJ)/*.d)

$(OBJ):
	@mkdir -p $(OBJ)

$(FONT_SRC): $(ROOT)/scripts/gen-blob.py $(OTF)
	@mkdir -p $(GEN)
	@$(ROOT)/scripts/gen-blob.py $@ OkapiaOutlineRegular $(word 1,$(OTF)) OkapiaOutlineBold $(word 2,$(OTF))

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
$(OBJ)/p_%.o: $(CHOST)/compat/%.cpp | $(OBJ) $(STR_IDS) $(KEYCODES_H) $(MACROMAN_H)
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
$(OBJ)/mz_%.o: $(MINIZ)/%.c | $(OBJ)
	@printf '  CC    %s\n' "$(notdir $<)"
	@$(CC) -O2 -w -I$(MINIZ) -Ishims -MMD -MP -c -o $@ $<

$(OBJ)/sl_%.o: $(MACEMU)/slirp/%.c | $(OBJ)
	@printf '  CC    slirp/%s\n' "$(notdir $<)"
	@$(CC) -O2 -w -Ishims/slirp -I$(MACEMU)/slirp -MMD -MP -c -o $@ $<
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
$(OBJ)/d_%.o: %.mm | $(OBJ) $(STR_IDS)
	@printf '  OBJCXX %s\n' "$(notdir $<)"
	@$(CXX) $(MMFLAGS) -fobjc-arc -MMD -MP -c -o $@ $<
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
