#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-cell-setup-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    cell_setup.cpp replacement.cpp world.cpp floor.cpp coarse_category.cpp test_cell_setup.cpp ../rules/ini_file.cpp -o "$binary"
"$binary" "$@"
