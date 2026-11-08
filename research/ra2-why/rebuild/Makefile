# RA2 engine rebuild - one command builds and tests every layer.
# Layers land one at a time; each must compile clean and pass its
# tests against the real game data in game/ (read-only).

CXX ?= g++
.DEFAULT_GOAL := all
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
SDL_FLAGS = $(shell pkg-config --cflags --libs sdl2)
NATIVE_SOURCES = src/app/platform.cpp src/app/main.cpp src/app/terrain_inputs.cpp src/app/scene.cpp src/app/vehicle_art.cpp \
    src/render/tmp_decode.cpp src/render/minimap.cpp src/loop/game_loop.cpp \
    src/fs/mix_archive.cpp src/fs/mix_bootstrap.cpp src/fs/filename_hash.cpp \
    src/gfx/palette.cpp src/gfx/shp_decode.cpp src/actors/actors.cpp \
    src/session/session.cpp src/session/map_geometry.cpp src/session/placement_terrain.cpp src/weapons/weapons.cpp src/weapons/occupant_order.cpp \
    src/ui/ui.cpp src/ui/shell.cpp src/ui/skirmish.cpp src/text/csf.cpp \
    src/pack/map_pack.cpp src/rules/ini_file.cpp src/map/map_file.cpp \
    src/sim/sim.cpp src/sim/random.cpp src/sim/factory.cpp src/sim/ground.cpp \
    src/sim/combat.cpp src/sim/target.cpp src/sim/distance.cpp src/sim/floor.cpp src/sim/path.cpp src/sim/cell_entry.cpp src/sim/production.cpp \
    src/sim/world.cpp src/sim/world_init.cpp src/sim/drive.cpp src/sim/movement.cpp src/sim/replacement.cpp src/sim/build_time.cpp src/voxel/voxel.cpp
NATIVE_OBJECTS = $(patsubst %.cpp,build/native/%.o,$(NATIVE_SOURCES))

build/native/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I src $(shell pkg-config --cflags sdl2) -MMD -MP -c $< -o $@

# Include dependencies for test objects as well as the native client.
-include $(wildcard build/native/src/*/*.d)

.PHONY: all test clean fs rules text map pack loop gfx hook run-fs run-rules run-text run-map run-pack run-loop run-gfx run-hook

all: test

.PHONY: native native-check platform run-platform catalog terrain-inputs scene actors ui audio voxel sim sim-move sim-replacement

FS_OBJECTS = build/native/src/fs/mix_archive.o build/native/src/fs/mix_bootstrap.o build/native/src/fs/filename_hash.o
GFX_OBJECTS = build/native/src/gfx/palette.o build/native/src/gfx/shp_decode.o
SIM_OBJECTS = $(filter build/native/src/sim/%.o,$(NATIVE_OBJECTS))
MAP_OBJECTS = build/native/src/rules/ini_file.o build/native/src/map/map_file.o build/native/src/pack/map_pack.o
SESSION_OBJECTS = build/native/src/session/session.o build/native/src/session/map_geometry.o build/native/src/session/placement_terrain.o

native-check: native platform terrain-inputs scene base-move actors ui audio voxel sim sim-move sim-route sim-path sim-entry-height sim-replacement sim-world sim-build-time sim-production session session-terrain weapons
	build/ra2-native --game-dir ../game --frames 3 --headless
	build/ra2-native --game-dir ../game --frames 3 --headless --yuri
	build/ra2-native --game-dir ../game --map ../analysis/extracted/maps/2PEAKS.MAP --frames 3 --headless
	build/ra2-native --game-dir ../game --map ../analysis/extracted/maps/2PEAKS.MAP --frames 3 --headless --yuri
	build/ra2-native --game-dir ../game --map ALL01T.MAP --frames 3 --headless
	build/ra2-native --game-dir ../game --map MP25MW.MAP --frames 3 --headless

actors: build/actors_test
	build/actors_test ../game

build/actors_test: build/native/src/actors/test_main.o build/native/src/actors/actors.o $(FS_OBJECTS) $(GFX_OBJECTS) build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

ui: build/ui_test
	build/ui_test ../game

build/ui_test: build/native/src/ui/ui.o build/native/src/ui/shell.o build/native/src/ui/skirmish.o build/native/src/ui/test_main.o build/native/src/text/csf.o build/native/src/rules/ini_file.o $(FS_OBJECTS) $(GFX_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

audio: build/audio_test build/audio_integration_test
	build/audio_test
	build/audio_integration_test ../game

build/audio_test: build/native/src/audio/audio.o build/native/src/audio/test_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/audio_integration_test: build/native/src/audio/audio.o build/native/src/audio/lookup.o build/native/src/audio/integration_test.o $(FS_OBJECTS) build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

# This legacy test source includes the filesystem header by its short name.
build/native/src/audio/integration_test.o: src/audio/integration_test.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I src/fs -MMD -MP -c $< -o $@

voxel: build/voxel_test
	build/voxel_test

build/voxel_test: build/native/src/voxel/voxel.o build/native/src/voxel/test_main.o $(FS_OBJECTS) build/native/src/gfx/palette.o
	$(CXX) $(CXXFLAGS) $^ -o $@

sim: build/sim_test
	build/sim_test ../analysis/extracted

build/sim_test: build/native/src/sim/test_main.o $(SIM_OBJECTS) $(MAP_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

sim-move: build/sim_move_test
	build/sim_move_test ../game ../analysis/extracted
	build/sim_move_test ../game ../analysis/extracted --base

build/sim_move_test: build/native/src/sim/test_move.o build/native/src/sim/test_inputs.o $(SIM_OBJECTS) $(MAP_OBJECTS) $(FS_OBJECTS) build/native/src/gfx/palette.o build/native/src/render/minimap.o build/native/src/render/tmp_decode.o
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

.PHONY: sim-route
sim-route: build/sim_route_test
	build/sim_route_test ../game/game.exe ../game/gamemd.exe

build/sim_route_test: build/native/src/sim/test_route.o build/native/src/sim/drive.o build/native/src/sim/ground.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: sim-path sim-entry-height
sim-path: build/sim_path_test
	build/sim_path_test ../game/game.exe ../game/gamemd.exe

build/sim_path_test: build/native/src/sim/test_path.o build/native/src/sim/path.o build/native/src/sim/cell_entry.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

sim-entry-height: build/sim_entry_height_test
	build/sim_entry_height_test

build/sim_entry_height_test: build/native/src/sim/test_entry_height.o build/native/src/sim/cell_entry.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/native/src/sim/test_inputs.o: src/sim/test_inputs.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I src/render -MMD -MP -c $< -o $@

sim-replacement: build/sim_replacement_test
	build/sim_replacement_test

build/sim_replacement_test: build/native/src/sim/test_replacement.o build/native/src/sim/replacement.o build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: sim-world
sim-world: build/sim_world_test
	build/sim_world_test

build/sim_world_test: build/native/src/sim/test_world_init.o build/native/src/sim/world_init.o build/native/src/sim/replacement.o build/native/src/sim/world.o build/native/src/sim/floor.o build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/terrain_depth_test: build/native/src/render/test_depth.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: sim-build-time
sim-build-time: build/sim_build_time_test
	build/sim_build_time_test --self-test

build/sim_build_time_test: build/native/src/sim/test_build_time.o build/native/src/sim/build_time.o build/native/src/sim/factory.o build/native/src/sim/random.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: sim-production
sim-production: build/sim_production_test
	build/sim_production_test

build/sim_production_test: build/native/src/sim/test_production.o build/native/src/sim/production.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: session
session: build/session_test
	build/session_test ../game

build/session_test: $(SESSION_OBJECTS) build/native/src/session/test_main.o build/native/src/sim/random.o build/native/src/sim/distance.o build/native/src/rules/ini_file.o $(FS_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: session-terrain
session-terrain: build/session_terrain_test
	build/session_terrain_test ../game
	build/session_terrain_test ../game --yuri C1A02MD.MAP

build/session_terrain_test: $(SESSION_OBJECTS) build/native/src/session/real_terrain_test.o build/native/src/sim/test_inputs.o $(SIM_OBJECTS) $(MAP_OBJECTS) $(FS_OBJECTS) build/native/src/gfx/palette.o build/native/src/render/minimap.o build/native/src/render/tmp_decode.o
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

build/session_geometry_cases: $(SESSION_OBJECTS) build/native/src/session/geometry_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_color_cases: $(SESSION_OBJECTS) build/native/src/session/color_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_unlimbo_cases: $(SESSION_OBJECTS) build/native/src/session/unlimbo_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_auxiliary_cases: $(SESSION_OBJECTS) build/native/src/session/auxiliary_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_chooser_cases: build/native/src/session/session.o build/native/src/session/chooser_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_base_unit_cases: build/native/src/session/session.o build/native/src/session/base_unit_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_house_cases: build/native/src/session/session.o build/native/src/session/house_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_budget_cases: build/native/src/session/session.o build/native/src/session/budget_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_placement_cases: build/native/src/session/session.o build/native/src/session/placement_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_unit_choice_cases: build/native/src/session/session.o build/native/src/session/unit_choice_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_unit_loop_cases: build/native/src/session/session.o build/native/src/session/unit_loop_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/session_mission_cases: build/native/src/session/session.o build/native/src/session/mission_cases.o build/native/src/sim/random.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: weapons
weapons: build/weapons_test build/weapons_asset_test build/weapons_lobber_asset_test
	build/weapons_test
	build/weapons_asset_test ../game
	build/weapons_lobber_asset_test ../game

build/weapons_test: build/native/src/weapons/weapons.o build/native/src/weapons/test_main.o build/native/src/sim/combat.o build/native/src/sim/target.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/weapons_asset_test: build/native/src/weapons/asset_test.o $(FS_OBJECTS) build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/weapons_lobber_asset_test: build/native/src/weapons/lobber_asset_test.o $(FS_OBJECTS) build/native/src/rules/ini_file.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/weapons_fractional_tick_test: build/native/src/weapons/fractional_tick_test.o build/native/src/weapons/weapons.o build/native/src/sim/combat.o build/native/src/sim/target.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/weapons_near_ground_test: build/native/src/weapons/near_ground_test.o build/native/src/weapons/weapons.o build/native/src/sim/test_inputs.o $(SIM_OBJECTS) $(MAP_OBJECTS) $(FS_OBJECTS) build/native/src/gfx/palette.o build/native/src/render/minimap.o build/native/src/render/tmp_decode.o
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

build/weapons_area_impact_test: build/native/src/weapons/area_impact_test.o build/native/src/weapons/weapons.o build/native/src/sim/combat.o build/native/src/sim/target.o build/native/src/sim/distance.o
	$(CXX) $(CXXFLAGS) $^ -o $@

build/weapons_occupant_test: build/native/src/weapons/occupant_order_test.o build/native/src/weapons/occupant_order.o
	$(CXX) $(CXXFLAGS) $^ -o $@

scene: build/scene_test
	build/scene_test ../game ../analysis/extracted/maps/2PEAKS.MAP

build/scene_test: build/native/src/app/scene.o build/native/src/app/vehicle_art.o build/native/src/app/terrain_inputs.o build/native/src/app/test_scene.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

.PHONY: base-move
base-move: build/base_move_test
	build/base_move_test ../game

build/base_move_test: build/native/src/app/scene.o build/native/src/app/vehicle_art.o build/native/src/app/terrain_inputs.o build/native/src/app/test_base_move.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

.PHONY: world-maps
world-maps: build/world_maps_test
	build/world_maps_test ../game

build/world_maps_test: build/native/src/app/terrain_inputs.o build/native/src/app/test_world_maps.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

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

.PHONY: render-check
render-check: build/render_test
	cd .. && rebuild/build/render_test

build/render_test: build/native/src/render/test_main.o $(filter-out build/native/src/app/%.o,$(NATIVE_OBJECTS))
	$(CXX) $(CXXFLAGS) $^ -ldl -o $@

build/ra2-native: $(NATIVE_OBJECTS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) $^ $(SDL_FLAGS) -ldl -o $@

test: fs rules text map pack loop gfx hook native-check
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

