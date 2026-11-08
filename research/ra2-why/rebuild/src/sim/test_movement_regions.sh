#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-movement-regions-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} regions.cpp movement_regions.cpp test_movement_regions.cpp -o "$binary"
"$binary"
