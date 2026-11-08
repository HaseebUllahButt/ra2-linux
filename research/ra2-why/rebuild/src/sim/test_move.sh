#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
shared=${RA2_SHARED_SOURCE:-/path/to/user/ra2/rebuild/src}
binary=$(mktemp /tmp/ra2-sim-move-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} -I "$shared/render" \
    sim.cpp random.cpp factory.cpp ground.cpp combat.cpp target.cpp distance.cpp floor.cpp world.cpp drive.cpp movement.cpp test_move.cpp test_inputs.cpp \
    ../rules/ini_file.cpp ../map/map_file.cpp ../pack/map_pack.cpp \
    "$shared/render/minimap.cpp" "$shared/render/tmp_decode.cpp" "$shared/gfx/palette.cpp" \
    "$shared/fs/mix_archive.cpp" "$shared/fs/mix_bootstrap.cpp" "$shared/fs/filename_hash.cpp" -ldl -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game}" "${2:-/path/to/user/ra2/analysis/extracted}" "${@:3}"
