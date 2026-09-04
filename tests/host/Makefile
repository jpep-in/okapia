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

all: specimen.ppm check

$(FONT_SRC): $(ROOT)/scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-font.py $@ $(BDF)

$(ICON_SRC): $(ROOT)/scripts/gen-icons.py $(ROOT)/assets/icons/system-icons.c
	@mkdir -p $(GEN)
	$(ROOT)/scripts/gen-icons.py $@ \
	    $(ROOT)/assets/icons/system-icons.c

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

# The measurements are part of the build, not something to remember to run: an
# asymmetry of one pixel is invisible on a screenshot and obvious at three. The
# behaviour joins them: what the focus does under a sequence of keys is decidable
# on a machine with no keyboard, so there is no reason to go and look.
check: check_geometry check_screen check_chooser check_pages
	./check_geometry
	./check_screen
	./check_chooser
	./check_pages

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
	      check_chooser check_pages \
	      specimen*.ppm chooser*.ppm var*.ppm

.PHONY: all view check clean
