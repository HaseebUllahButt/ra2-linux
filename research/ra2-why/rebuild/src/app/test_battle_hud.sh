#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d /tmp/ra2-battle-hud.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror \
    -I .. battle_hud.cpp test_battle_hud.cpp ../fs/mix_bootstrap.cpp ../fs/mix_archive.cpp ../fs/filename_hash.cpp ../gfx/shp_decode.cpp ../gfx/palette.cpp ../rules/ini_file.cpp -o "$build/test_battle_hud"
"$build/test_battle_hud" "$@"
