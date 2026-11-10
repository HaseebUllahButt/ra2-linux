#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-movement-scenes.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    prototype_movement.cpp path.cpp path_smoothing.cpp cell_entry.cpp ../weapons/occupant_order.cpp ../app/battle_shroud.cpp prototype_shroud.cpp \
    ../rules/ini_file.cpp drive.cpp distance.cpp random.cpp world.cpp ground.cpp test_movement_scenes.cpp \
    -Wl,--gc-sections -o "$binary"
"$binary"
