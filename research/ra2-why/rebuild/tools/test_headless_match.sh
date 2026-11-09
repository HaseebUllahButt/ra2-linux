#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
repo=$(git -C "$here" rev-parse --show-toplevel)
revision=${HEADLESS_REVISION:-master}
game=${1:-/path/to/user/ra2/game}
frames=${2:-1000}
build=$(mktemp -d /tmp/ra2-headless.XXXXXX)
trap 'rm -rf "$build"' EXIT
# Freeze the selected revision. Never change or build the live master checkout.
git -C "$repo" archive "$revision" rebuild/src | tar -x -C "$build"
src="$build/rebuild/src"
files=()
for name in prototype_ai prototype_movement prototype_economy prototype_victory prototype_production production_runtime production build_time damage_runtime random combat target distance drive ground world world_init cell_setup coarse_category replacement floor; do
 files+=("$src/sim/$name.cpp")
done
for name in unit_death detonation muzzle projectile_damage cannon_line cannon_range fire_eligibility fire_timer techno_damage weapons occupant_order object_damage; do
 files+=("$src/weapons/$name.cpp")
done
for name in session/world/setup session/world/world session/session session/map_geometry session/placement_terrain render/minimap render/tmp_decode gfx/palette fs/mix_archive fs/mix_bootstrap fs/filename_hash rules/ini_file map/map_file pack/map_pack; do
 files+=("$src/$name.cpp")
done
printf 'Source revision: %s\n' "$(git -C "$repo" rev-parse "$revision")"
status=0
for mode in normal sanitized; do
 flags=()
 if [[ $mode == sanitized ]]; then flags=(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer); fi
 "${CXX:-g++}" -std=c++20 -O1 -g1 -ffunction-sections -fdata-sections -I "$src" -I "$src/app" -I "$src/render" "${flags[@]}" "${files[@]}" "$here/headless_match.cpp" -Wl,--gc-sections -ldl -o "$build/$mode"
 echo "Build: $mode"
 ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=print_stacktrace=1 timeout "${HEADLESS_TIMEOUT:-600}" "$build/$mode" "$game" "$frames" || status=1
done
exit "$status"
