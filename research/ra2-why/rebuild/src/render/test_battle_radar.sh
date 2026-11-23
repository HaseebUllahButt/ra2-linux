#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-battle-radar.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror ${SIM_TEST_FLAGS:-} -I .. \
    battle_radar.cpp test_battle_radar.cpp minimap.cpp tmp_decode.cpp \
    ../gfx/palette.cpp ../map/map_file.cpp ../pack/map_pack.cpp \
    ../rules/ini_file.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp \
    ../fs/filename_hash.cpp -ldl -o "$binary"
"$binary" "$game_dir"
