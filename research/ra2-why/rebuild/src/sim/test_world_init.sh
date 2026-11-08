#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-world-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    world_init.cpp replacement.cpp world.cpp floor.cpp test_world_init.cpp ../rules/ini_file.cpp -o "$binary"
"$binary"
