#!/usr/bin/env bash
set -eu
# Usage: test_battle_shroud.sh [base-game-directory]. With a directory the
# last block reads the installed base RULES.INI.
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-}
if [[ -n "$game_dir" && "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"
build=$(mktemp -d /tmp/ra2-battle-shroud.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror ${BATTLE_SHROUD_TEST_FLAGS:-} -I .. \
    battle_shroud.cpp test_battle_shroud.cpp ../sim/prototype_shroud.cpp ../rules/ini_file.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp \
    -o "$build/test_battle_shroud"
if [[ -n "$game_dir" ]]; then "$build/test_battle_shroud" "$game_dir"; else "$build/test_battle_shroud"; fi
