#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-original-live-route.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections ${SIM_TEST_FLAGS:-} -I .. \
    prototype_movement.cpp path.cpp path_smoothing.cpp cell_entry.cpp distance.cpp drive.cpp random.cpp world.cpp ground.cpp \
    ../weapons/occupant_order.cpp ../rules/ini_file.cpp test_original_live_route.cpp \
    -Wl,--gc-sections -o "$binary"
# Find the original game directory in this checkout or a parent checkout.
exe=""
dir="$PWD"
for _ in 1 2 3 4 5; do
    dir=$(dirname "$dir")
    if [ -f "$dir/game-full/game.exe" ]; then exe="$dir/game-full/game.exe"; break; fi
    if [ -f "$dir/game/game.exe" ]; then exe="$dir/game/game.exe"; break; fi
done
"$binary" "${1:-$exe}"
