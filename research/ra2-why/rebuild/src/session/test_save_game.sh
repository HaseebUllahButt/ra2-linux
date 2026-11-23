#!/usr/bin/env bash
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")/../sim"
shared=$(cd .. && pwd)
build=$(mktemp -d /tmp/ra2-prototype-real.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I "$shared" -I "$shared/app" -I "$shared/render" \
    prototype_movement.cpp drive.cpp random.cpp ground.cpp world.cpp world_init.cpp cell_setup.cpp coarse_category.cpp replacement.cpp floor.cpp distance.cpp \
    ../session/world/setup.cpp ../session/world/world.cpp ../session/session.cpp ../session/map_geometry.cpp ../session/placement_terrain.cpp \
    ../weapons/occupant_order.cpp ../render/minimap.cpp ../render/tmp_decode.cpp ../gfx/palette.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp \
    ../rules/ini_file.cpp ../map/map_file.cpp ../pack/map_pack.cpp ../session/save_game.cpp ../session/test_save_game.cpp -Wl,--gc-sections -ldl -o "$build/test"
"$build/test" "${1:-/path/to/user/ra2/game}" "$build"
