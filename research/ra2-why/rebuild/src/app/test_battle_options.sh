#!/usr/bin/env bash
set -eu
# Usage: test_battle_options.sh [base_game_directory_with_game.exe]
cd "$(dirname "$0")"
build=$(mktemp -d /tmp/ra2-battle-options.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++20 -O1 -Wall -Wextra -Werror -I .. \
    battle_options.cpp battle_options_art.cpp test_battle_options.cpp \
    ../ui/ui.cpp ../ui/shell.cpp ../ui/skirmish.cpp ../text/csf.cpp ../rules/ini_file.cpp ../pack/map_pack.cpp \
    ../gfx/shp_decode.cpp ../gfx/palette.cpp ../fs/filename_hash.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp \
    -o "$build/test_battle_options"
"$build/test_battle_options" "$@"
