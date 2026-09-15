#
# Makefile — render the firmware's interface on the development machine.
#
# The firmware's drawing code has no Circle dependency on purpose, so it builds
# here with the host compiler and every component can be looked at without a
# kernel, a card or an emulator. AGENTS.md asks for host tests wherever they are
# possible; for an interface, "possible" means exactly this.
#
#   make            build and render specimen.ppm at 1x
#   make view       render at 2x and open it
#

ROOT     = ../..
FIRMWARE = $(ROOT)/src/firmware
GEN      = $(ROOT)/build/generated
FONTDIR  = $(ROOT)/assets/fonts
BDF      = $(FONTDIR)/helvR10-75.bdf $(FONTDIR)/helvB10-75.bdf $(FONTDIR)/helvR12-75.bdf $(FONTDIR)/helvB12-75.bdf $(FONTDIR)/helvR14-75.bdf $(FONTDIR)/helvB14-75.bdf $(FONTDIR)/helvR24-75.bdf $(FONTDIR)/helvB24-75.bdf $(FONTDIR)/helvR14-100.bdf $(FONTDIR)/helvB14-100.bdf $(FONTDIR)/helvR24-100.bdf $(FONTDIR)/helvB24-100.bdf

FONT_SRC = $(GEN)/okapia_font_data.cpp
ICON_SRC = $(GEN)/okapia_icons_data.cpp
STR_SRC  = $(GEN)/okapia_strings_data.cpp
STR_IDS  = $(GEN)/okapia_strings_ids.h
LIBSRCS  = $(wildcard $(FIRMWARE)/*.cpp) $(FONT_SRC) $(ICON_SRC) $(STR_SRC)
LIBOBJS  = $(notdir $(LIBSRCS:.cpp=.o))

CXXFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter -I$(FIRMWARE) -I$(GEN) -std=c++14

VPATH = $(FIRMWARE):$(GEN):.

# Two files of the platform layer build here too, and they are the two where a
# mistake costs data rather than pixels: prefs_circle.cpp, which can silently
# drop somebody's settings, and hfs_volume_circle.cpp, which reads volumes we
# must never damage. Neither touches Circle for anything but the log, so
# shims/okapia_circle.h — force-included ahead of the real one, whose include
# guard then swallows it — is the whole cost. They come with upstream's prefs
# tables, so the checks run against the real keyword lists and not a copy.
CIRCLE   = $(ROOT)/src/circle
MACEMU   = $(ROOT)/external/macemu/BasiliskII/src
LIBHFS   = $(ROOT)/external/hfsutils/libhfs

PLATFORM_CXXFLAGS = -O2 -Wall -Wextra -Wno-unused-parameter -std=c++14 \
    -include shims/okapia_circle.h -Ishims -I$(CIRCLE) -I$(CIRCLE)/sheepshaver -I$(MACEMU)/include \
    -I$(MACEMU)/Unix -I$(MACEMU)/uae_cpu_2021 -I$(LIBHFS) \
    -DDIRECT_ADDRESSING -DFPU_UAE
PLATFORM_OBJS = p_prefs_circle.o p_hfs_volume_circle.o p_prefs.o p_prefs_items.o \
                p_compositor_circle.o p_video_sizes_circle.o p_rom_chime.o \
                p_mac_layout.o
HFS_SRCS      = $(wildcard $(LIBHFS)/*.c)
HFS_OBJS      = $(addprefix hfs_,$(notdir $(HFS_SRCS:.c=.o)))

all: specimen.ppm check

$(FONT_SRC): $(ROOT)/scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-font.py $@ $(BDF)

$(ICON_SRC): $(ROOT)/scripts/gen-icons.py $(ROOT)/assets/icons/system-icons.c \
              $(ROOT)/assets/icons/cdrom-icon.c
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-icons.py $@ \
	    $(ROOT)/assets/icons/system-icons.c \
	    $(ROOT)/assets/icons/cdrom-icon.c

$(STR_SRC) $(STR_IDS): $(ROOT)/scripts/gen-strings.py $(ROOT)/assets/strings.tsv
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-strings.py $(STR_SRC) $(STR_IDS) $(ROOT)/assets/strings.tsv

# -MMD -MP, and the .d files pulled back in below: without them a change to a
# header rebuilds nothing and the link mixes two layouts of the same struct.
%.o: %.cpp $(FONT_SRC) $(ICON_SRC) $(STR_SRC) $(STR_IDS)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

# The generated sources get rules of their own rather than leaning on VPATH: a
# pattern rule cannot match a file that does not exist yet, so on a fresh clone —
# where build/generated is empty — make declared it had no rule for these and
# stopped. It only ever worked here because the files were already lying around.
okapia_font_data.o: $(FONT_SRC)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

okapia_icons_data.o: $(ICON_SRC)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

okapia_strings_data.o: $(STR_SRC) $(STR_IDS)
	$(CXX) $(CXXFLAGS) -MMD -MP -c -o $@ $<

-include $(wildcard *.d)

render_specimen: $(LIBOBJS) render_specimen.o
	$(CXX) -o $@ $^

check_geometry: $(LIBOBJS) check_geometry.o
	$(CXX) -o $@ $^

check_screen: $(LIBOBJS) check_screen.o
	$(CXX) -o $@ $^

render_chooser: $(LIBOBJS) render_chooser.o chooser_sample.o
	$(CXX) -o $@ $^

check_chooser: $(LIBOBJS) check_chooser.o chooser_sample.o
	$(CXX) -o $@ $^

render_pages: $(LIBOBJS) render_pages.o pages_sample.o
	$(CXX) -o $@ $^

check_pages: $(LIBOBJS) check_pages.o pages_sample.o
	$(CXX) -o $@ $^

# The platform objects carry a p_ prefix and libhfs an hfs_ one: without them
# prefs.cpp from upstream and prefs_circle.cpp from here would both want to be
# prefs.o, and the second would quietly win.
p_%.o: $(CIRCLE)/%.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

# The engine-specific half of the platform layer. mac_layout is pure arithmetic
# and touches neither Circle nor macemu, which is exactly why it is checked here
# rather than discovered on a card.
p_%.o: $(CIRCLE)/sheepshaver/%.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

p_%.o: $(MACEMU)/%.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

hfs_%.o: $(LIBHFS)/%.c
	$(CC) -O2 -w -I$(LIBHFS) -MMD -MP -c -o $@ $<

# An explicit rule, because the pattern above would build it with the firmware's
# include paths and it needs the platform's.
check_platform.o: check_platform.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

check_platform: check_platform.o $(PLATFORM_OBJS) $(HFS_OBJS)
	$(CXX) -o $@ $^

# video_blit.cpp is upstream's and lives one directory deeper than the p_ rule
# above can name, so it gets its own. It is the only display code Okapia does
# not write, and patches/macemu/0003 is why it is measured.
p_video_blit.o: $(MACEMU)/CrossPlatform/video_blit.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

# video_blit.h sits beside its .cpp rather than in include/, so the test needs
# that directory on the path where the .cpp did not.
check_blit.o: check_blit.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -I$(MACEMU)/CrossPlatform -MMD -MP -c -o $@ $<

check_blit: check_blit.o p_video_blit.o
	$(CXX) -o $@ $^

# The file-name converters. They need the generated MacRoman table, which the
# kernel build also makes — the rule is here too so this directory stands alone.
MACROMAN_H = $(GEN)/macroman_table.h
$(MACROMAN_H): $(ROOT)/scripts/gen-macroman.py
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-macroman.py $@

p_mac_encoding_circle.o: $(MACROMAN_H) $(CIRCLE)/mac_encoding_circle.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -I$(GEN) -MMD -MP -c -o $@ $(CIRCLE)/mac_encoding_circle.cpp

check_encoding.o: check_encoding.cpp
	$(CXX) $(PLATFORM_CXXFLAGS) -MMD -MP -c -o $@ $<

check_encoding: check_encoding.o p_mac_encoding_circle.o
	$(CXX) -o $@ $^

# The measurements are part of the build, not something to remember to run: an
# asymmetry of one pixel is invisible on a screenshot and obvious at three. The
# behaviour joins them: what the focus does under a sequence of keys is decidable
# on a machine with no keyboard, so there is no reason to go and look.
check: check_geometry check_screen check_chooser check_pages check_platform check_blit \
       check_encoding
	./check_geometry
	./check_screen
	./check_chooser
	./check_pages
	./check_platform
	./check_blit
	./check_encoding

# Every page, the count being the flow's to decide: naming them here again is
# how the second page went stale the day a third appeared.
specimen.ppm: render_specimen render_chooser render_pages
	./render_specimen specimen.ppm 1
	./render_chooser chooser.ppm 1
	./render_pages page 1

view: render_specimen
	./render_specimen specimen-2x.ppm 2 0
	@command -v open >/dev/null && open specimen-2x.ppm || echo "wrote specimen-2x.ppm"

clean:
	rm -f *.o *.d render_specimen render_chooser render_pages check_geometry check_screen \
	      check_chooser check_pages check_platform check_platform.tmp check_blit \
	      check_encoding \
	      specimen*.ppm chooser*.ppm var*.ppm

.PHONY: all view check clean
