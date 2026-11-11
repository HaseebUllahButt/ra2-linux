CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
.PHONY: all test clean
all: build/libra2adapter.a
build:
	mkdir -p $@
build/surface.o: src/surface.c include/ra_surface.h | build
	nice -n 19 $(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
build/present_sdl.o: src/present_sdl.c include/ra_present.h include/ra_surface.h | build
	nice -n 19 $(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -fPIC -c $< -o $@
build/libra2adapter.a: build/surface.o build/present_sdl.o
	$(AR) rcs $@ $^
build/test_surface: tests/test_surface.c src/surface.c include/ra_surface.h | build
	nice -n 19 $(CC) $(CPPFLAGS) $(CFLAGS) tests/test_surface.c src/surface.c -o $@
build/test_present: tests/test_present.c src/surface.c src/present_sdl.c include/ra_present.h | build
	nice -n 19 $(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) tests/test_present.c src/surface.c src/present_sdl.c $(SDL_LIBS) -o $@
test: build/test_surface build/test_present
	./build/test_surface
	SDL_VIDEODRIVER=dummy ./build/test_present
clean:
	rm -rf build

WCC ?= ../tools/wcc
SDL_WIN ?= $(firstword $(wildcard .reference/SDL2-*/i686-w64-mingw32))
.PHONY: windows
windows: build/ddraw.dll build/test_ddraw.exe
build/ddraw.dll: src/windows/ddraw.c src/windows/ddraw_slots.inc src/windows/ddraw.def src/surface.c src/present_sdl.c | build
	nice -n 19 $(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Iinclude -I$(SDL_WIN)/include/SDL2 -shared src/windows/ddraw.c src/surface.c src/present_sdl.c src/windows/ddraw.def $(SDL_WIN)/lib/libSDL2.dll.a -o $@
	cp $(SDL_WIN)/bin/SDL2.dll build/SDL2.dll
build/test_ddraw.exe: tests/test_ddraw.c | build
	nice -n 19 $(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -o $@

.PHONY: test-tools
test-tools:
	python3 tests/test_compare_frames.py

build/test_display.exe: tests/test_display.c | build
	nice -n 19 $(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -luser32 -o $@

build/test_color.exe: tests/test_color.c | build
	nice -n 19 $(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -luser32 -o $@
