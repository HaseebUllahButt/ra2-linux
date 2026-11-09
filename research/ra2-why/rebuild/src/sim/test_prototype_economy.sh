#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-prototype-economy.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
 prototype_economy.cpp prototype_movement.cpp test_prototype_economy.cpp ../rules/ini_file.cpp \
 ../weapons/occupant_order.cpp random.cpp world.cpp ground.cpp \
 ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -Wl,--gc-sections -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game}"
