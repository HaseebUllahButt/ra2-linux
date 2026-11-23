#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-unit-visibility.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    unit_visibility.cpp world.cpp random.cpp ground.cpp ../session/world/world.cpp test_unit_visibility.cpp -Wl,--gc-sections -o "$binary"
"$binary"
