#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-prototype-speed.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    prototype_movement.cpp drive.cpp ../weapons/occupant_order.cpp random.cpp world.cpp ground.cpp ../rules/ini_file.cpp test_prototype_speed.cpp -Wl,--gc-sections -o "$binary"
"$binary"
