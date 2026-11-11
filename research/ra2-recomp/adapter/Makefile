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
