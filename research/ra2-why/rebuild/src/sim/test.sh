#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    sim.cpp random.cpp factory.cpp ground.cpp combat.cpp target.cpp distance.cpp test_main.cpp ../rules/ini_file.cpp ../map/map_file.cpp ../pack/map_pack.cpp -ldl -o "$binary"
"$binary" "${1:-/path/to/user/ra2/analysis/extracted}" "${@:2}"
