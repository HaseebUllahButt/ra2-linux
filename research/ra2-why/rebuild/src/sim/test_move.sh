#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
shared=${RA2_SHARED_SOURCE:-/path/to/user/ra2/rebuild/src}
build=$(mktemp -d /tmp/ra2-sim-move-test.XXXXXX)
trap 'rm -rf "$build"' EXIT
# The read-only asset reader is shared code. Keep its RSA work outside sim instrumentation.
asset_objects=()
index=0
for source in test_inputs.cpp "$shared/render/minimap.cpp" "$shared/render/tmp_decode.cpp" "$shared/gfx/palette.cpp" \
    "$shared/fs/mix_archive.cpp" "$shared/fs/mix_bootstrap.cpp" "$shared/fs/filename_hash.cpp"; do
    object="$build/assets$index.o"
    ${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 -I "$shared/render" -c "$source" -o "$object"
    asset_objects+=("$object")
    index=$((index+1))
done
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    sim.cpp random.cpp factory.cpp ground.cpp combat.cpp target.cpp distance.cpp floor.cpp world.cpp world_init.cpp replacement.cpp drive.cpp movement.cpp test_move.cpp \
    ../rules/ini_file.cpp ../map/map_file.cpp ../pack/map_pack.cpp "${asset_objects[@]}" -ldl -o "$build/test"
"$build/test" "${1:-/path/to/user/ra2/game}" "${2:-/path/to/user/ra2/analysis/extracted}" "${@:3}"
