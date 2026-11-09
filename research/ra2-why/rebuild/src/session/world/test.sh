#!/usr/bin/env bash
set -eu
export PYTHONDONTWRITEBYTECODE=1
cd "$(dirname "$0")"
shared=${RA2_SHARED_SOURCE:-$(cd ../.. && pwd)}
game=${1:-${RA2_GAME_DIR:-$shared/../../game}}
build=$(mktemp -d /tmp/ra2-session-world.XXXXXX)
trap 'rm -rf "$build"' EXIT
common=(world.cpp ../session.cpp ../map_geometry.cpp ../placement_terrain.cpp
    "$shared"/sim/{random,distance,coarse_category,cell_setup,replacement,world,floor}.cpp
    "$shared/rules/ini_file.cpp" "$shared/weapons/occupant_order.cpp")
for test in test_snapshot house_case constructor_case placement_case cell_setup_case; do
    ${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} -I "$shared" -I "$shared/app" \
        "${common[@]}" "$test.cpp" -o "$build/$test"
done
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ../map_geometry.cpp ../geometry_cases.cpp -o "$build/geometry_cases"
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} cell_occupancy.cpp cell_occupancy_case.cpp -o "$build/cell_occupancy_case"
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} bridge_footprint.cpp bridge_footprint_case.cpp -o "$build/bridge_footprint_case"
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} speed_type.cpp speed_type_case.cpp -o "$build/speed_type_case"
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} -I "$shared" movement_zone.cpp movement_zone_case.cpp -o "$build/movement_zone_case"
${CXX:-c++} -std=c++17 -O0 -Wall -Wextra -Werror ${SESSION_WORLD_TEST_FLAGS:-} unit_flags.cpp unit_flags_case.cpp -o "$build/unit_flags_case"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -I "$shared" -I "$shared/app" -I "$shared/render" \
    world.cpp speed_type.cpp movement_zone.cpp unit_flags.cpp ../session.cpp ../map_geometry.cpp ../placement_terrain.cpp target_tile_case.cpp \
    "$shared"/sim/{test_inputs,world,world_init,cell_setup,coarse_category,replacement,floor,random,distance}.cpp \
    "$shared/weapons/occupant_order.cpp" \
    "$shared"/render/{minimap,tmp_decode}.cpp "$shared/gfx/palette.cpp" \
    "$shared"/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp "$shared/rules/ini_file.cpp" \
    "$shared/map/map_file.cpp" "$shared/pack/map_pack.cpp" -ldl -o "$build/target_tile_case"
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -I "$shared" -I "$shared/app" -I "$shared/render" \
    real_map_case.cpp "$shared"/sim/test_inputs.cpp "$shared"/render/{minimap,tmp_decode}.cpp \
    "$shared/gfx/palette.cpp" "$shared"/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp \
    "$shared/rules/ini_file.cpp" "$shared/map/map_file.cpp" "$shared/pack/map_pack.cpp" \
    -ldl -o "$build/real_map_case"
"$build/test_snapshot"
"$build/cell_setup_case"
"$build/cell_occupancy_case" > /dev/null
"$build/bridge_footprint_case" > /dev/null
"$build/speed_type_case"
"$build/movement_zone_case"
"$build/unit_flags_case"
"$build/real_map_case" "$game"
"$build/target_tile_case" "$game"
"$build/target_tile_case" "$game" --stage
if [ "${2:-}" = "--original" ]; then
    export PYTHONPATH="$shared/sim${PYTHONPATH:+:$PYTHONPATH}"
    ${PYTHON:-python3} original_base_local_check.py "$game/game.exe" "$build/geometry_cases"
    ${PYTHON:-python3} original_house_check.py "$game/game.exe" "$build/house_case"
    ${PYTHON:-python3} original_constructor_check.py "$game/game.exe" "$build/constructor_case"
    ${PYTHON:-python3} original_joint_check.py "$game/game.exe" "$build/constructor_case"
    ${PYTHON:-python3} original_placement_check.py "$game/game.exe" "$build/placement_case"
    ${PYTHON:-python3} original_real_map_check.py "$game/game.exe" "$build/placement_case" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_cell_occupancy_check.py "$game/game.exe" "$build/cell_occupancy_case"
    ${PYTHON:-python3} original_bridge_check.py "$game/game.exe" "$build/bridge_footprint_case"
    ${PYTHON:-python3} original_map_order_check.py "$game/game.exe"
    ${PYTHON:-python3} original_reset_order_check.py "$game/game.exe"
    ${PYTHON:-python3} original_resize_check.py "$game/game.exe" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_overlay_registry_check.py "$game/game.exe"
    ${PYTHON:-python3} original_overlay_map_check.py "$game/game.exe" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_tile_type_check.py "$game/game.exe"
    ${PYTHON:-python3} original_target_bounds_check.py "$game/game.exe" "$build/target_tile_case" "$game"
    ${PYTHON:-python3} original_land_table_check.py "$game/game.exe" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_speed_type_check.py "$game/game.exe" "$build/real_map_case" "$game" "$build/speed_type_case"
    ${PYTHON:-python3} original_movement_zone_check.py "$game/game.exe" "$build/real_map_case" "$game" "$build/movement_zone_case"
    ${PYTHON:-python3} original_unit_flags_check.py "$game/game.exe" "$build/real_map_case" "$game" "$build/unit_flags_case"
    ${PYTHON:-python3} original_type_ctor_fields_check.py "$game/game.exe" "$build/unit_flags_case"
    ${PYTHON:-python3} original_object_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_techno_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-bool
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-art
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-integer
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-float
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-speed
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-speed-wheel
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-zone-subterranean
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-placement
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-restricted-water
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-armor
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-vhpscan
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-vhpscan-strong
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-compare
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-compare-strong
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-token
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-token-strong
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-section
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-copy
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-color
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-tokens
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-real-format
    ${PYTHON:-python3} original_unit_reader_205_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game" --connected-color-override
    ${PYTHON:-python3} original_unit_dispatch_check.py "$game/game.exe" "$build/unit_flags_case" "$build/real_map_case" "$game"
    ${PYTHON:-python3} original_placement_type_check.py "$game/game.exe" "$build/real_map_case" "$game" "$build/unit_flags_case"
    ${PYTHON:-python3} original_sight_rule_check.py "$game/game.exe" "$build/real_map_case" "$game"
    ${PYTHON:-python3} "$shared/sim/original_cell_setup_check.py" "$game/game.exe" --base --vectors > "$build/original-setup-vectors"
    "$build/cell_setup_case" --vectors > "$build/staged-setup-vectors"
    cmp "$build/original-setup-vectors" "$build/staged-setup-vectors"
fi
