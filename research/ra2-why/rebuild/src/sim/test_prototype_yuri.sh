#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-yuri.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -Wall -Wextra -Werror -O1 ${SIM_TEST_FLAGS:-} -I .. \
    prototype_yuri.cpp test_prototype_yuri.cpp ../rules/ini_file.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -o "$binary"
"$binary" "$game_dir"
