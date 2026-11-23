#!/usr/bin/env bash
set -eu
# Usage: test_prototype_shroud.sh [base-game-directory]. The last checks read
# the installed base RULES.INI; without a directory only the pure checks run.
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-shroud.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} -I .. \
    prototype_shroud.cpp test_prototype_shroud.cpp ../rules/ini_file.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -o "$binary"
"$binary" "$game_dir"
