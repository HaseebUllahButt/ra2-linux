#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
game_exe=${1:-$root/game/game.exe}
if [[ "$game_exe" != /* ]]; then game_exe="$PWD/$game_exe"; fi
scratch=$(mktemp -d /tmp/ra2-kill-promotion.XXXXXX)
trap 'rm -rf "$scratch"' EXIT
cd "$root"
sources=(sim/test_kill_promotion sim/damage_runtime sim/prototype_special sim/random sim/combat sim/target sim/distance rules/ini_file
    weapons/weapons weapons/object_damage weapons/techno_damage weapons/projectile_damage weapons/muzzle weapons/detonation
    weapons/fire_timer weapons/cannon_line weapons/cannon_range weapons/fire_eligibility weapons/unit_death)
objects=()
# One compiler process at a time. Never read or write shared build outputs.
for source in "${sources[@]}"; do
    object="$scratch/${source//\//_}.o"
    "${CXX:-g++}" -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -I rebuild/src -c "rebuild/src/$source.cpp" -o "$object"
    objects+=("$object")
done
"${CXX:-g++}" "${objects[@]}" -Wl,--gc-sections -o "$scratch/test"
"$scratch/test" "$game_exe"
python rebuild/src/sim/verify_kill_promotion_x86.py "$game_exe" "$scratch/test"
