#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-postprocessed-path-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} path.cpp path_smoothing.cpp cell_entry.cpp distance.cpp test_postprocessed_path.cpp -o "$binary"
"$binary" "${1:-../../../game/game.exe}" "${2:-../../../game/gamemd.exe}"
