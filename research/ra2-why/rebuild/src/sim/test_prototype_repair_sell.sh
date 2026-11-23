#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-repair-sell.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror ${SIM_TEST_FLAGS:-} -I .. \
    prototype_repair_sell.cpp test_prototype_repair_sell.cpp ../rules/ini_file.cpp \
    ../session/world/world.cpp world.cpp ../sim/random.cpp ../sim/coarse_category.cpp \
    ../sim/cell_setup.cpp ../sim/floor.cpp ../sim/distance.cpp ../sim/replacement.cpp \
    ../session/session.cpp ../session/map_geometry.cpp ../session/placement_terrain.cpp \
    ../weapons/occupant_order.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -o "$binary" -ldl
"$binary" "$game_dir"
