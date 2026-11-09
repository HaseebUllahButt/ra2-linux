#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-nearest-projection-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    nearest_cell.cpp cell_projection.cpp distance.cpp test_nearest_projection.cpp -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game/game.exe}" "${2:-/path/to/user/ra2/game/gamemd.exe}" "${@:3}"
