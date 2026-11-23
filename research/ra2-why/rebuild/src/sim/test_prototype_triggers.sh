#!/usr/bin/env bash
set -eu
# Usage: test_prototype_triggers.sh [game-directory]. With a game directory the
# test also reads every map in the MIX archives.
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-}
if [[ -n "$game_dir" && "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-triggers.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror ${SIM_TEST_FLAGS:-} -I .. \
    prototype_triggers.cpp test_prototype_triggers.cpp ../rules/ini_file.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -o "$binary"
"$binary" $game_dir
