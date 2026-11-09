#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-live-cell-setup.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    live_cell_setup.cpp cell_setup.cpp coarse_category.cpp replacement.cpp world.cpp floor.cpp random.cpp ground.cpp \
    ../session/world/world.cpp ../rules/ini_file.cpp test_live_cell_setup.cpp -Wl,--gc-sections -o "$binary"
"$binary"
