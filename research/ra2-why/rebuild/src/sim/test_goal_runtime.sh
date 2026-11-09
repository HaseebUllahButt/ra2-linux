#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-goal-runtime.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    goal_runtime.cpp unit_mark.cpp unit_visibility.cpp live_cell_setup.cpp cell_setup.cpp coarse_category.cpp replacement.cpp floor.cpp ../rules/ini_file.cpp ../weapons/occupant_order.cpp component_lookup.cpp foot_path.cpp goal_path.cpp path.cpp path_smoothing.cpp cell_entry.cpp \
    hierarchy.cpp hierarchy_route.cpp hierarchy_retry.cpp retry_edges.cpp regions.cpp movement_regions.cpp search_memory.cpp drive.cpp ground.cpp distance.cpp random.cpp world.cpp world_init.cpp \
    ../session/world/world.cpp test_goal_runtime.cpp -Wl,--gc-sections -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game/game.exe}" "${@:2}"
