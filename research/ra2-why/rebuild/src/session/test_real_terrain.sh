#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
shared=${RA2_SHARED_SOURCE:-$(cd .. && pwd)}
game=${1:-$(cd ../../.. && pwd)/game}
build=$(mktemp -d /tmp/ra2-session-terrain.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -I "$shared/app" -I "$shared/render" \
    session.cpp map_geometry.cpp placement_terrain.cpp real_terrain_test.cpp \
    "$shared"/sim/{test_inputs,world,world_init,replacement,floor,random,distance}.cpp \
    "$shared"/render/{minimap,tmp_decode}.cpp "$shared/gfx/palette.cpp" \
    "$shared"/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp \
    "$shared/rules/ini_file.cpp" "$shared/map/map_file.cpp" "$shared/pack/map_pack.cpp" \
    -ldl -o "$build/test"
"$build/test" "$game"
if [ -n "${RA2_SESSION_YURI_MAP:-}" ]; then
    "$build/test" "$game" --yuri "$RA2_SESSION_YURI_MAP"
fi
if [ "${2:-}" = "--original" ]; then
    printf '%s\n' '#!/bin/sh' 'if [ -n "${RA2_TERRAIN_YURI:-}" ]; then exec "$RA2_TERRAIN_TEST" "$RA2_GAME_DIR" --cases --yuri "$RA2_TERRAIN_YURI"; fi' 'exec "$RA2_TERRAIN_TEST" "$RA2_GAME_DIR" --cases' > "$build/cases"
    chmod +x "$build/cases"
    export RA2_TERRAIN_TEST="$build/test" RA2_GAME_DIR="$game"
    export PYTHONPATH="$shared/sim${PYTHONPATH:+:$PYTHONPATH}"
    python3 original_geometry_check.py "$game/game.exe" "$build/cases"
    python3 original_geometry_check.py "$game/gamemd.exe" "$build/cases"
    if [ -n "${RA2_SESSION_YURI_MAP:-}" ]; then
        export RA2_TERRAIN_YURI="$RA2_SESSION_YURI_MAP"
        python3 original_geometry_check.py "$game/gamemd.exe" "$build/cases"
    fi
fi
