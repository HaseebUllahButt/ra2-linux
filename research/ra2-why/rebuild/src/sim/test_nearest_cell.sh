#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-nearest-cell-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    nearest_cell.cpp distance.cpp test_nearest_cell.cpp -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game/game.exe}" "${2:-/path/to/user/ra2/game/gamemd.exe}" "${@:3}"
