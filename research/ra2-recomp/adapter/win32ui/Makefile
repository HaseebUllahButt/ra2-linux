# win32ui (J14): portable window and dialog code.
#   make            ra2ui.dll for the original game under Wine (32-bit, zig cc)
#   make linux      libra2ui.a for a 32-bit Linux build (needs 32-bit SDL2; later)
WCC ?= ../../tools/wcc
SDL_WIN ?= $(firstword $(wildcard ../.reference/SDL2-*/i686-w64-mingw32))
SRC := src/window.c src/message.c src/dialog.c src/defwnd.c src/controls.c src/resource.c src/wsprintf.c src/platform_sdl.c src/sdl_native.c
HDR := src/ui.h src/ui_api.inc src/input.h
CFLAGS := -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type
.PHONY: all clean
# OUT=build-dev builds a second copy, so a recording that uses build/ra2ui.dll is not disturbed.
OUT ?= build
all: $(OUT)/ra2ui.dll
$(OUT):
	mkdir -p $(OUT)
$(OUT)/ra2ui.dll: $(SRC) src/win/dllmain.c $(HDR) | $(OUT)
	$(WCC) $(CFLAGS) -Isrc -I$(SDL_WIN)/include/SDL2 -shared $(SRC) src/win/dllmain.c $(SDL_WIN)/lib/libSDL2.dll.a -o $@
	cp $(SDL_WIN)/bin/SDL2.dll $(OUT)/SDL2.dll
clean:
	rm -rf build

# Candidate control ports. Keep the production source until golden checks pass.
PORT_SRC := $(filter-out src/controls.c,$(SRC)) $(wildcard src/controls/*.c)
build/ra2ui-port.dll: $(PORT_SRC) src/win/dllmain.c $(HDR) src/controls/compat.h | build
	nice -n 19 $(WCC) $(CFLAGS) -Isrc -I$(SDL_WIN)/include/SDL2 -shared $(PORT_SRC) src/win/dllmain.c $(SDL_WIN)/lib/libSDL2.dll.a -o $@
build/ctltest.exe: tests/ctltest.c | build
	$(WCC) -O2 -g -Wall -Wextra tests/ctltest.c -luser32 -lgdi32 -lcomctl32 -o $@
