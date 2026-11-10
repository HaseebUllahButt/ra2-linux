#!/usr/bin/env bash
set -eu
# Usage: test_prototype_psychic_sensor.sh [base-game-directory]
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2-test-host/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-psychic-sensor.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror ${SIM_TEST_FLAGS:-} -ffunction-sections -fdata-sections -I .. \
    prototype_psychic_sensor.cpp distance.cpp \
    ../rules/ini_file.cpp \
    test_prototype_psychic_sensor.cpp -Wl,--gc-sections -ldl -o "$binary"
"$binary" "$game_dir"
