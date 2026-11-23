#!/usr/bin/env bash
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")"
shared=$(cd .. && pwd)
build=$(mktemp -d /tmp/ra2-goal-runtime-real.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I "$shared" -I "$shared/app" -I "$shared/render" \
    goal_runtime.cpp unit_mark.cpp component_lookup.cpp foot_path.cpp goal_path.cpp path.cpp path_smoothing.cpp cell_entry.cpp \
    hierarchy.cpp hierarchy_route.cpp hierarchy_retry.cpp retry_edges.cpp regions.cpp movement_regions.cpp search_memory.cpp drive.cpp ground.cpp distance.cpp random.cpp world.cpp world_init.cpp \
    ../session/world/setup.cpp ../session/world/world.cpp ../session/session.cpp ../session/map_geometry.cpp ../session/placement_terrain.cpp \
    cell_setup.cpp coarse_category.cpp replacement.cpp floor.cpp ../weapons/occupant_order.cpp \
    ../render/minimap.cpp ../render/tmp_decode.cpp ../gfx/palette.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp \
    ../rules/ini_file.cpp ../map/map_file.cpp ../pack/map_pack.cpp test_goal_runtime_real.cpp -Wl,--gc-sections -ldl -o "$build/test"
"$build/test" "${1:-/path/to/user/ra2/game}"
