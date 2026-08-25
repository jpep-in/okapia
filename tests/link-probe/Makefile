#
# Makefile — spike: link the whole Basilisk II core against Circle.
#
# The point is not a working kernel. It is the list of undefined symbols: exactly
# what src/circle/ has to provide, measured rather than guessed.
#
include ../../external/circle-stdlib/Config.mk

MACEMU  = ../../external/macemu/BasiliskII/src
GEN     = ../../build/generated
OKAPIA  = ../../src/circle
CIRCLEH = ../../external/circle-stdlib/libs/circle

# bincue.cpp needs SDL; it is an upstream option, off by default.
CORE_SRCS  = $(filter-out $(MACEMU)/bincue.cpp,$(wildcard $(MACEMU)/*.cpp))
CPU_SRCS   = $(wildcard $(MACEMU)/uae_cpu_2021/*.cpp) \
             $(MACEMU)/uae_cpu_2021/fpu/fpu_uae.cpp \
             $(MACEMU)/CrossPlatform/video_blit.cpp
GEN_SRCS   = $(GEN)/cpudefs.cpp $(GEN)/cpustbl.cpp $(GEN)/cpufunctbl.cpp
DUMMY_SRCS = $(wildcard $(MACEMU)/dummy/*.cpp)
COMPAT_SRCS = $(wildcard $(OKAPIA)/compat/*.cpp)
PLATFORM_SRCS = $(wildcard $(OKAPIA)/*.cpp)

DEFINES  = -DDIRECT_ADDRESSING -DFPU_UAE -DAARCH=64 -D__circle__ -D_GNU_SOURCE
INCLUDES = -I$(OKAPIA) -I$(OKAPIA)/compat -I$(GEN) \
           -I$(MACEMU)/include -I$(MACEMU)/uae_cpu_2021 -I$(MACEMU)/Unix \
           -I$(MACEMU)/CrossPlatform \
           $(CIRCLE_STDLIB_INCLUDES) \
           -isystem $(CIRCLE_STDLIB_INSTALL_DIR)/include \
           -isystem $(CIRCLEH)/include -isystem $(CIRCLEH)/addon

# -fexceptions: Circle defaults to -fno-exceptions, the core throws for 68k faults.
CORE_FLAGS = $(ARCH) -std=gnu++17 -fexceptions -fno-rtti -O2 -Wno-narrowing

OBJDIR = obj

ALL_SRCS = $(CORE_SRCS) $(CPU_SRCS) $(GEN_SRCS) $(DUMMY_SRCS) $(COMPAT_SRCS) $(PLATFORM_SRCS)
ALL_OBJS = $(addprefix $(OBJDIR)/,$(notdir $(ALL_SRCS:.cpp=.o))) \
           $(addprefix $(OBJDIR)/cpuemu,$(addsuffix .o,1 2 3 4 5 6 7 8))

VPATH = $(OKAPIA):$(OKAPIA)/compat:$(MACEMU):$(MACEMU)/uae_cpu_2021:$(MACEMU)/uae_cpu_2021/fpu:$(MACEMU)/CrossPlatform:$(MACEMU)/dummy:$(GEN)

$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@printf '  CC  %s\n' "$(notdir $<)"
	@$(CXX) $(CORE_FLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@ 2>$(OBJDIR)/$(notdir $<).log \
	  || { printf '      FAILED, see %s\n' "$(OBJDIR)/$(notdir $<).log"; }

$(OBJDIR)/cpuemu%.o: $(GEN)/cpuemu.cpp | $(OBJDIR)
	@printf '  CC  cpuemu part %s\n' "$*"
	@$(CXX) $(CORE_FLAGS) $(DEFINES) -DPART_$* $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# Undefined symbols are the deliverable, so link errors are expected and kept.
probe: $(ALL_OBJS)
	@printf '\n==> Undefined symbols the platform layer must provide\n\n'
	@aarch64-none-elf-ld -r -o $(OBJDIR)/core-combined.o $(ALL_OBJS)
	@aarch64-none-elf-nm -u $(OBJDIR)/core-combined.o \
	  | sed 's/^ *U //' \
	  | aarch64-none-elf-c++filt \
	  | sort -u > undefined.txt
	@wc -l < undefined.txt | xargs printf '  %s undefined symbols, listed in undefined.txt\n'

clean:
	rm -rf $(OBJDIR) undefined.txt

.PHONY: probe clean
