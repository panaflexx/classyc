# SPACEWAY 3000 — ClassyC + NanoVG galaxy map demo
#
# From this directory:
#   make           interactive window (JIT, -eb)
#   make smoke     headless smoke test
#   make test      syntax-ish build + smoke
#   make run       alias for default
#
# From repo root:
#   make -C example/spaceway3k
#   make -C example/spaceway3k smoke

ROOT      := $(abspath ../..)
CLASSYC   := $(ROOT)/bin/classyc
INCLUDE   := $(ROOT)/include

NANOVG_SRC := $(ROOT)/PROJ/nanogui2/ext/nanovg/src
NANOVG_EX  := $(ROOT)/PROJ/nanogui2/ext/nanovg/example
LIBNANOVG  := ./libnanovg.bmir

SRC := spaceway3k.cy
UNAME := $(shell uname)

# Match the working nanovg classyc flags (see PROJ/.../run-classyc.sh).
# Darwin: OpenGL is a framework, not libGL; GLFW/freetype come from Homebrew.
CYFLAGS := \
	-I $(INCLUDE) \
	-fno-ownership -fno-exceptions -O3 \
	-DSTBI_NO_SIMD -DSTBIR_NO_SIMD \
	-DFONS_USE_FREETYPE -DNVG_BUILD -DGL_GLEXT_PROTOTYPES \
	-I $(NANOVG_SRC) -I $(NANOVG_EX)
ifeq ($(UNAME),Darwin)
  BREW := $(shell brew --prefix 2>/dev/null)
  CYFLAGS += -DGLFW_INCLUDE_GLCOREARB -framework OpenGL
  ifneq ($(BREW),)
    CYFLAGS += -I$(BREW)/include -L$(BREW)/lib
    ifneq ($(wildcard $(BREW)/opt/freetype/include/freetype2),)
      CYFLAGS += -I$(BREW)/opt/freetype/include/freetype2
    endif
  endif
  CYFLAGS += -lglfw -lfreetype -lharfbuzz
else
  CYFLAGS += \
	-I/usr/include/freetype2 -I/usr/include/libpng16 -I/usr/include/harfbuzz \
	-lGL -lglfw -lfreetype -lharfbuzz
endif

.PHONY: all run smoke profile test check-lib clean help

all: run

help:
	@echo "SPACEWAY 3000 targets:"
	@echo "  make / make run   interactive NanoVG map"
	@echo "  make smoke        headless ~180 frames"
	@echo "  make profile      uncapped headless + section timing"
	@echo "  make test         require libnanovg.bmir + smoke"
	@echo "  make clean        remove local artifacts"
	@echo ""
	@echo "Build libnanovg.bmir once: make libnanovg"

check-lib:
	@test -x $(CLASSYC) || (echo "missing $(CLASSYC) — build classyc first"; exit 1)
	@test -f $(LIBNANOVG) || (echo "missing $(LIBNANOVG)"; echo "run: make help"; exit 1)

run: check-lib
	$(CLASSYC) $(CYFLAGS) $(LIBNANOVG) $(SRC) -eb

smoke: check-lib
	SPACEWAY3K_SMOKE=1 $(CLASSYC) $(CYFLAGS) $(LIBNANOVG) $(SRC) -eb

profile: check-lib
	SPACEWAY3K_PROFILE=1 SPACEWAY3K_FRAMES=300 SPACEWAY3K_SEED=42 \
	  $(CLASSYC) $(CYFLAGS) $(LIBNANOVG) $(SRC) -eb

test: check-lib smoke
	@echo "OK — spaceway3k smoke passed"

# Optional: rebuild the precompiled NanoVG BMIR from this Makefile.
libnanovg:
	$(CLASSYC) -c $(CYFLAGS) $(NANOVG_SRC)/nanovg.c -o $(LIBNANOVG)

clean:
	rm -f *.bmir a.out a.bmir

.PHONY: libnanovg
