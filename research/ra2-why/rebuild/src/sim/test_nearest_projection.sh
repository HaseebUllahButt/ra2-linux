#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-nearest-projection-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    nearest_cell.cpp cell_projection.cpp distance.cpp test_nearest_projection.cpp -o "$binary"
game=${RA2_GAME_DIR:-$(cd ../../.. && pwd)/game}
"$binary" "${1:-$game/game.exe}" "${2:-$game/gamemd.exe}" "${@:3}"
