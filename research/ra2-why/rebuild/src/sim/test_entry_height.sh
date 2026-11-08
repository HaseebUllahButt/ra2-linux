#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-entry-height.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    cell_entry.cpp test_entry_height.cpp -o "$binary"
"$binary"
