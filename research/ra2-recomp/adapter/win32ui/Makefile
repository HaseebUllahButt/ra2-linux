# win32ui (J14): portable window and dialog code.
#   make            ra2ui.dll for the original game under Wine (32-bit, zig cc)
#   make linux      libra2ui.a for a 32-bit Linux build (needs 32-bit SDL2; later)
WCC ?= ../../tools/wcc
SDL_WIN ?= $(firstword $(wildcard ../.reference/SDL2-*/i686-w64-mingw32))
SRC := src/window.c src/message.c src/dialog.c src/defwnd.c src/controls.c src/resource.c src/wsprintf.c src/platform_sdl.c src/sdl_native.c
HDR := src/ui.h src/ui_api.inc src/input.h
CFLAGS := -O2 -g -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type
.PHONY: all clean
all: build/ra2ui.dll
build:
	mkdir -p build
build/ra2ui.dll: $(SRC) src/win/dllmain.c $(HDR) | build
	nice -n 19 $(WCC) $(CFLAGS) -Isrc -I$(SDL_WIN)/include/SDL2 -shared $(SRC) src/win/dllmain.c $(SDL_WIN)/lib/libSDL2.dll.a -o $@
	cp $(SDL_WIN)/bin/SDL2.dll build/SDL2.dll
clean:
	rm -rf build
