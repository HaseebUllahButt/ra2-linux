#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-filtered-path-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} path.cpp cell_entry.cpp distance.cpp test_filtered_path.cpp -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game/game.exe}" "${2:-/path/to/user/ra2/game/gamemd.exe}"
