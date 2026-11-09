#!/usr/bin/env bash
set -eu
# Usage: test_prototype_superweapon.sh [base-game-directory]
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-superweapon.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror ${SIM_TEST_FLAGS:-} -I .. \
    prototype_superweapon.cpp combat.cpp random.cpp distance.cpp \
    ../rules/ini_file.cpp test_prototype_superweapon.cpp -o "$binary"
"$binary" "$game_dir"
