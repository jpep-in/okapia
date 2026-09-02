#
# Makefile — the theme specimen kernel.
#
# Circle plus the firmware's drawing code, and nothing else: no Basilisk core,
# no card, no emulator. It builds in seconds, which is what a design still being
# decided needs, and looking at the theme then costs neither a card nor a risk
# to one.
#
include ../../../external/circle-stdlib/Config.mk

FIRMWARE = ../../../src/firmware
GEN      = ../../../build/generated
FONTDIR  = ../../../assets/fonts
BDF      = $(FONTDIR)/helvR10-75.bdf $(FONTDIR)/helvB10-75.bdf $(FONTDIR)/helvR12-75.bdf $(FONTDIR)/helvB12-75.bdf $(FONTDIR)/helvR14-75.bdf $(FONTDIR)/helvB14-75.bdf $(FONTDIR)/helvR24-75.bdf $(FONTDIR)/helvB24-75.bdf $(FONTDIR)/helvR14-100.bdf $(FONTDIR)/helvB14-100.bdf $(FONTDIR)/helvR24-100.bdf $(FONTDIR)/helvB24-100.bdf

FIRMWARE_SRCS = $(wildcard $(FIRMWARE)/*.cpp)
FIRMWARE_OBJS = $(addprefix obj/,$(notdir $(FIRMWARE_SRCS:.cpp=.o)))

# Everything through obj/, including the sources next to this Makefile. Left to
# Circle's own rule they carry no -MMD, so they track no header — and a struct
# that gained a field left one object on the old layout while the link
# succeeded. The symptom was an arrow key arriving as Escape.
OBJS = obj/specimen_main.o obj/specimen_kernel.o obj/okapia_input.o $(FIRMWARE_OBJS) \
       obj/okapia_font_data.o obj/okapia_icons_data.o

include $(CIRCLEHOME)/Rules.mk

INCLUDE := -I$(FIRMWARE) -I$(GEN) $(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)
LIBS := $(CIRCLE_STDLIB_LIBS)

# The fonts are converted from Circle's own console faces rather than typed out,
# the same habit as scripts/gen-keycodes.py for the key table. When a
# proportional face is adopted, only the generator changes.
FONT_SRC = $(GEN)/okapia_font_data.cpp
$(FONT_SRC): ../../../scripts/gen-font.py $(BDF)
	@mkdir -p $(GEN)
	@../../../scripts/gen-font.py $@ $(BDF)

ICON_SRC = $(GEN)/okapia_icons_data.cpp
ICONDIR  = ../../../assets/icons
$(ICON_SRC): ../../../scripts/gen-icons.py $(ICONDIR)/system-icons.c
	@mkdir -p $(GEN)
	@../../../scripts/gen-icons.py $@ $(ICONDIR)/system-icons.c

obj:
	@mkdir -p obj

# -MMD -MP, and the .d files pulled back in below. Without them a change to a
# header recompiles nothing, and the link silently mixes objects built against
# two different layouts of the same struct — which shows up as a jump into the
# font tables, not as a compile error. AGENTS.md records the same trap for the
# emulator objects; a private rule reintroduces it unless it carries this.
obj/%.o: $(FIRMWARE)/%.cpp | obj
	@printf '  CPP   firmware/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) -MMD -MP -c -o $@ $<

obj/%.o: $(FIRMWARE)/circle/%.cpp | obj
	@printf '  CPP   firmware/circle/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) -MMD -MP -c -o $@ $<

obj/%.o: %.cpp | obj
	@printf '  CPP   %s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) -MMD -MP -c -o $@ $<

obj/okapia_font_data.o: $(FONT_SRC) | obj
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) -MMD -MP -c -o $@ $<

obj/okapia_icons_data.o: $(ICON_SRC) | obj
	@printf '  CPP   generated/%s\n' "$(notdir $<)"
	@$(CPP) $(CPPFLAGS) -MMD -MP -c -o $@ $<

-include $(wildcard obj/*.d)

obj/specimen_kernel.o obj/specimen_main.o: $(FONT_SRC) $(ICON_SRC)

# Circle's Rules.mk already owns "clean" with a single colon, so ours is its
# own target rather than an addition to it.
specimen-clean:
	@rm -rf obj *.o *.d kernel*.img kernel*.elf kernel*.lst kernel*.map

.PHONY: specimen-clean
