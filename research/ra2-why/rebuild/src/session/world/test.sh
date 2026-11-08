#!/usr/bin/env bash
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")"
shared=${RA2_SHARED_SOURCE:-/path/to/user/ra2/rebuild/src}
game=${1:-/path/to/user/ra2/game}
build=$(mktemp -d /tmp/ra2-session-world.XXXXXX)
trap 'rm -rf "$build"' EXIT
common=(world.cpp ../session.cpp ../map_geometry.cpp ../placement_terrain.cpp
    "$shared"/sim/{random,distance,coarse_category}.cpp "$shared/weapons/occupant_order.cpp")
for test in test_snapshot house_case constructor_case placement_case; do
    ${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror -I "$shared" -I "$shared/app" \
        "${common[@]}" "$test.cpp" -o "$build/$test"
done
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ../map_geometry.cpp ../geometry_cases.cpp -o "$build/geometry_cases"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -I "$shared" -I "$shared/app" -I "$shared/render" \
    real_map_case.cpp "$shared"/sim/test_inputs.cpp "$shared"/render/{minimap,tmp_decode}.cpp \
    "$shared/gfx/palette.cpp" "$shared"/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp \
    "$shared/rules/ini_file.cpp" "$shared/map/map_file.cpp" "$shared/pack/map_pack.cpp" \
    -ldl -o "$build/real_map_case"
"$build/test_snapshot"
"$build/real_map_case" "$game"
if [ "${2:-}" = "--original" ]; then
    export PYTHONPATH="$shared/sim${PYTHONPATH:+:$PYTHONPATH}"
    python3 original_base_local_check.py "$game/game.exe" "$build/geometry_cases"
    python3 original_house_check.py "$game/game.exe" "$build/house_case"
    python3 original_constructor_check.py "$game/game.exe" "$build/constructor_case"
    python3 original_joint_check.py "$game/game.exe" "$build/constructor_case"
    python3 original_placement_check.py "$game/game.exe" "$build/placement_case"
    python3 original_real_map_check.py "$game/game.exe" "$build/placement_case"
fi
