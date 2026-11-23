#!/usr/bin/env bash
set -euo pipefail
task_root=$(cd "$(dirname "$0")/../.." && pwd)
task_build=$(mktemp -d)
trap 'rm -rf "$task_build"' EXIT
task_game=${1:-"$task_root/../game"}
task_cxx=${CXX:-g++}
task_flags=(-std=c++17 -O2 -Wall -Wextra -Werror -I "$task_root/src")
task_sources=(fs/filename_hash fs/mix_archive fs/mix_bootstrap gfx/palette gfx/shp_decode rules/ini_file actors/actors)
task_objects=()
for task_source in "${task_sources[@]}"; do
    task_object="$task_build/${task_source//\//_}.o"
    "$task_cxx" "${task_flags[@]}" -c "$task_root/src/$task_source.cpp" -o "$task_object"
    task_objects+=("$task_object")
done
"$task_cxx" "${task_flags[@]}" "$task_root/src/app/infantry_art.cpp" \
    "$task_root/src/app/test_infantry_art.cpp" "${task_objects[@]}" -o "$task_build/infantry_art_test"
"$task_build/infantry_art_test" "$task_game"
"$task_cxx" "${task_flags[@]}" "$task_root/src/sim/prototype_infantry.cpp" \
    "$task_root/src/sim/test_prototype_infantry.cpp" "${task_objects[@]}" -o "$task_build/prototype_infantry_test"
"$task_build/prototype_infantry_test" "$task_game"
python3 "$task_root/src/app/test_infantry_art_original.py" "$task_game/game.exe"
