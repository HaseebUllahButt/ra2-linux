# RA2 engine rebuild - one command builds and tests every layer.
# Layers land one at a time; each must compile clean and pass its
# tests against the real game data in game/ (read-only).

CXX ?= g++
.DEFAULT_GOAL := all
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
SDL_FLAGS = $(shell pkg-config --cflags --libs sdl2)
NATIVE_SOURCES = src/app/platform.cpp src/app/main.cpp src/app/terrain_inputs.cpp \
    src/render/tmp_decode.cpp src/render/minimap.cpp src/loop/game_loop.cpp \
    src/fs/mix_archive.cpp src/fs/mix_bootstrap.cpp src/fs/filename_hash.cpp \
    src/gfx/palette.cpp src/pack/map_pack.cpp src/rules/ini_file.cpp src/map/map_file.cpp \
    src/sim/sim.cpp src/sim/random.cpp src/sim/factory.cpp src/sim/ground.cpp \
    src/sim/combat.cpp src/sim/target.cpp src/sim/distance.cpp src/sim/floor.cpp
NATIVE_OBJECTS = $(patsubst %.cpp,build/native/%.o,$(NATIVE_SOURCES))

build/native/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(shell pkg-config --cflags sdl2) -MMD -MP -c $< -o $@

-include $(NATIVE_OBJECTS:.o=.d) build/native/src/app/test_platform.d

.PHONY: all test clean fs rules text map pack loop gfx hook run-fs run-rules run-text run-map run-pack run-loop run-gfx run-hook

all: test

.PHONY: native platform run-platform catalog terrain-inputs

terrain-inputs: build/terrain_inputs_test
	build/terrain_inputs_test ../game ../analysis/extracted/maps/2PEAKS.MAP

build/terrain_inputs_test: build/native/src/app/terrain_inputs.o build/native/src/app/test_terrain_inputs.o build/native/src/sim/floor.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

platform: run-platform

run-platform: build/platform_test
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/platform_test

build/platform_test: build/native/src/app/platform.o build/native/src/app/test_platform.o
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ $(SDL_FLAGS) -o $@

native: build/ra2-native

catalog: build/catalog_test
	build/catalog_test ../game ../analysis/extracted/maps

build/catalog_test: build/native/src/render/catalog_test.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

build/ra2-native: $(NATIVE_OBJECTS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ $(SDL_FLAGS) -ldl -o $@

test: fs rules text map pack loop gfx hook
	@echo "== all layer tests passed =="

fs: run-fs

rules: run-rules

text: run-text

loop: run-loop

gfx: run-gfx

hook: run-hook

run-fs:
	$(CXX) $(CXXFLAGS) -I src/fs src/fs/mix_archive.cpp \
		src/fs/mix_bootstrap.cpp src/fs/filename_hash.cpp \
		src/fs/test_main.cpp -o src/fs/mix_test
	src/fs/mix_test --self-test

run-rules:
	$(CXX) $(CXXFLAGS) -I src/rules src/rules/ini_file.cpp \
		src/rules/rules_reader.cpp src/rules/test_main.cpp \
		-o src/rules/rules_test
	cd .. && rebuild/src/rules/rules_test

run-hook:
	$(CXX) $(CXXFLAGS) -I hook hook/test_trampoline.cpp \
		hook/trampoline.cpp -o hook/test_trampoline
	hook/test_trampoline

clean:
	rm -f src/fs/mix_test src/rules/rules_test hook/test_trampoline \
		src/fs/crypto_test

run-text:
	$(CXX) $(CXXFLAGS) -I src/text src/text/csf.cpp \
		src/text/test_csf.cpp -o src/text/test_csf
	src/text/test_csf ../analysis/extracted/ra2.csf
	src/text/test_csf ../analysis/extracted/ra2md.csf

run-loop:
	$(CXX) $(CXXFLAGS) -I src/loop src/loop/game_loop.cpp \
		src/loop/test_main.cpp -o src/loop/loop_test
	src/loop/loop_test

run-gfx:
	$(CXX) $(CXXFLAGS) -I src/gfx -I src/fs src/gfx/palette.cpp \
		src/gfx/shp_decode.cpp src/gfx/test_main.cpp \
		src/fs/mix_archive.cpp src/fs/mix_bootstrap.cpp \
		src/fs/filename_hash.cpp -o src/gfx/gfx_test
	cd .. && rebuild/src/gfx/gfx_test

shp_view:
	$(CXX) $(CXXFLAGS) -I src/gfx -I src/fs src/gfx/shp_view.cc \
		src/gfx/palette.cpp src/gfx/shp_decode.cpp \
		src/fs/mix_archive.cpp src/fs/mix_bootstrap.cpp \
		src/fs/filename_hash.cpp -o src/gfx/shp_view

map: run-map

pack: run-pack

run-pack:
	mkdir -p build
	$(CXX) $(CXXFLAGS) -I src/pack -I src/rules src/pack/map_pack.cpp \
		src/rules/ini_file.cpp src/pack/test_main.cpp -ldl -o build/pack_test
	build/pack_test ../analysis/extracted/maps

run-map:
	$(CXX) $(CXXFLAGS) -I src/map -I src/rules src/map/map_file.cpp \
		src/rules/ini_file.cpp src/pack/map_pack.cpp src/map/test_main.cpp \
		-ldl -o src/map/map_test
	src/map/map_test ../analysis/extracted/maps/2PEAKS.MAP

