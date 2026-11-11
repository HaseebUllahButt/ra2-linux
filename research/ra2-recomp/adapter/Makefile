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
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@
build/present_sdl.o: src/present_sdl.c include/ra_present.h include/ra_surface.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -fPIC -c $< -o $@
build/libra2adapter.a: build/surface.o build/present_sdl.o
	$(AR) rcs $@ $^
build/test_surface: tests/test_surface.c src/surface.c include/ra_surface.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_surface.c src/surface.c -o $@
build/test_present: tests/test_present.c src/surface.c src/present_sdl.c include/ra_present.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) tests/test_present.c src/surface.c src/present_sdl.c $(SDL_LIBS) -o $@
test: build/test_surface build/test_present
	./build/test_surface
	SDL_VIDEODRIVER=dummy ./build/test_present
clean:
	rm -rf build

WCC ?= ../tools/wcc
SDL_WIN ?= $(firstword $(wildcard .reference/SDL2-*/i686-w64-mingw32))
.PHONY: windows
windows: build/ddraw.dll build/test_ddraw.exe
build/ddraw-legacy.dll: src/windows/ddraw.c src/windows/ddraw_slots.inc src/windows/ddraw.def src/surface.c src/present_sdl.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Iinclude -I$(SDL_WIN)/include/SDL2 -shared src/windows/ddraw.c src/surface.c src/present_sdl.c src/windows/ddraw.def $(SDL_WIN)/lib/libSDL2.dll.a -luser32 -o $@
	cp $(SDL_WIN)/bin/SDL2.dll build/SDL2.dll
build/test_ddraw.exe: tests/test_ddraw.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -o $@

.PHONY: test-tools
test-tools:
	python3 tests/test_compare_frames.py

build/test_display.exe: tests/test_display.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -luser32 -o $@

build/test_color.exe: tests/test_color.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -luser32 -o $@

build/dsound-legacy.dll: src/windows/dsound.c src/windows/dsound_slots.inc src/windows/dsound.def src/audio.c include/ra_audio.h | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-misleading-indentation -Iinclude -I$(SDL_WIN)/include/SDL2 -shared src/windows/dsound.c src/audio.c src/windows/dsound.def $(SDL_WIN)/lib/libSDL2.dll.a -o $@
	cp $(SDL_WIN)/bin/SDL2.dll build/SDL2.dll

build/test_audio: tests/test_audio.c src/audio.c include/ra_audio.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_audio.c src/audio.c -lm -o $@
build/test_dsound.exe: tests/test_dsound.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -o $@

FFMPEG_WIN ?= build/ffmpeg-win/install
build/binkw32-legacy.dll: src/windows/binkw32.c src/windows/binkw32.def src/bink.c include/ra_bink.h | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation -Iinclude -I$(SDL_WIN)/include/SDL2 -I$(FFMPEG_WIN)/include -shared src/windows/binkw32.c src/bink.c src/windows/binkw32.def $(FFMPEG_WIN)/lib/libavformat.a $(FFMPEG_WIN)/lib/libavcodec.a $(FFMPEG_WIN)/lib/libswscale.a $(FFMPEG_WIN)/lib/libswresample.a $(FFMPEG_WIN)/lib/libavutil.a $(SDL_WIN)/lib/libSDL2.dll.a -lbcrypt -o $@

build/test_bink.exe: tests/test_bink.c include/ra_bink.h | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -Iinclude $< -o $@

build/test_bink_native: tests/test_bink_native.c src/bink.c include/ra_bink.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-misleading-indentation $(SDL_CFLAGS) $(shell pkg-config --cflags libavformat libavcodec libavutil libswscale libswresample) tests/test_bink_native.c src/bink.c $(SDL_LIBS) $(shell pkg-config --libs libavformat libavcodec libavutil libswscale libswresample) -o $@

build/capture_ddraw.dll: tests/capture_ddraw.c tests/auto_menu.inc | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror -shared $< -o $@
build/capture_launch.exe: tests/capture_launch.c ../shim/launch.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Wno-missing-field-initializers $< -o $@

build/click_control.exe: tests/click_control.c | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Werror $< -luser32 -o $@

HLE_SOURCES = upstream/pcrecomp/ddraw.c upstream/pcrecomp/dsound.c upstream/pcrecomp/bink.c src/hle/bridge_win.c src/hle/platform_win.c src/hle/exports_win.c src/surface.c src/present_sdl.c
build/ra2adapter.dll: $(HLE_SOURCES) compat/win32hle.h compat/pthread.h src/hle/backend.def | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Wno-unused-function -DHLE_WITH_FFMPEG -DADAPTER_NATIVE_WINDOWS -Icompat -Iinclude -I$(SDL_WIN)/include -I$(SDL_WIN)/include/SDL2 -I$(FFMPEG_WIN)/include -shared $(HLE_SOURCES) src/hle/backend.def $(FFMPEG_WIN)/lib/libavformat.a $(FFMPEG_WIN)/lib/libavcodec.a $(FFMPEG_WIN)/lib/libswresample.a $(FFMPEG_WIN)/lib/libavutil.a $(SDL_WIN)/lib/libSDL2.dll.a -lbcrypt -luser32 -lgdi32 -o $@
	cp $(SDL_WIN)/bin/SDL2.dll build/SDL2.dll
build/ddraw.dll: src/windows/ddraw.def src/hle/proxy_win.c build/ra2adapter.dll
	$(WCC) -shared -DRA_FORWARD_DDRAW src/hle/proxy_win.c src/windows/ddraw.def -o $@
build/dsound.dll: src/windows/dsound.def src/hle/proxy_win.c build/ra2adapter.dll
	$(WCC) -shared -DRA_FORWARD_DSOUND src/hle/proxy_win.c src/windows/dsound.def -o $@
build/binkw32.dll: src/windows/binkw32.def src/hle/proxy_win.c build/ra2adapter.dll
	$(WCC) -shared -DRA_FORWARD_BINK src/hle/proxy_win.c src/windows/binkw32.def -o $@

build/test_hle_stream.exe: tests/test_hle_stream.c upstream/pcrecomp/dsound.c src/hle/bridge_win.c compat/win32hle.h compat/pthread.h | build
	$(WCC) -O2 -g -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -Icompat -I$(SDL_WIN)/include -I$(SDL_WIN)/include/SDL2 tests/test_hle_stream.c upstream/pcrecomp/dsound.c src/hle/bridge_win.c $(SDL_WIN)/lib/libSDL2.dll.a -o $@
