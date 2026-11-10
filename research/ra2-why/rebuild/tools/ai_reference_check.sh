#!/usr/bin/env bash
# Compare our computer opponent with the ORIGINAL game, per map and level.
# Runs our matching headless matches and compares each with the original
# census log in $ORACLE_CENSUS/<isle|canyon|heart>-<easy|medium|brutal>/census.log
# (recorded with ~/Projects/ra2-oracle/bin/orig_census.py). Matches without an
# original log are run but not compared.
#
# Setup of the reference runs: the human (Americans) builds only Power Plant,
# Ore Refinery, Barracks and War Factory and never attacks; one computer
# (Russians); 10000 credits; 10 start units; Super Weapons off.
#
# Usage (from rebuild/): tools/ai_reference_check.sh
# Environment: GAME_DIR (../game-full), ORACLE_CENSUS, FRAMES (40000), JOBS (6),
#              OUT (build/ai-reference), TOP (6 differences for each match).
set -euo pipefail
cd "$(dirname "$0")/.."
GAME_DIR=${GAME_DIR:-../game-full}
ORACLE_CENSUS=${ORACLE_CENSUS:-$HOME/Projects/ra2-oracle/runs/census}
FRAMES=${FRAMES:-40000}
JOBS=${JOBS:-6}
OUT=${OUT:-build/ai-reference}
TOP=${TOP:-6}
BIN=build/ra2-native
[ -x "$BIN" ] || { echo "missing $BIN: run make build/ra2-native" >&2; exit 2; }
[ -d "$GAME_DIR" ] || { echo "missing game folder $GAME_DIR" >&2; exit 2; }
mkdir -p "$OUT"

MAPS="isle:MP02T2.MAP canyon:MP09T3.MAP heart:MP05T4.MAP"
LEVELS="easy:e medium:m brutal:h"

run_one() { # name map level-letter
    local name=$1 map=$2 level=$3
    if RA2_HOUSE_CENSUS=1 RA2_IMMORTAL_PLAYER=1 RA2_PASSIVE_PLAYER=2 RA2_TEST_UNIT_COUNT=10 RA2_TEST_SUPER_WEAPONS=0 \
        RA2_TEST_OPPONENTS=$level SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        nice "$BIN" --game-dir "$GAME_DIR" --skirmish --map "$map" --headless --autoplay \
        --frames "$FRAMES" >"$OUT/$name.out" 2>"$OUT/$name.log"; then
        echo "ran $name"
    else
        echo "ran $name (exit $?; the census lines are still used)"
    fi
}
export -f run_one
export BIN GAME_DIR FRAMES OUT

jobs_list=()
for m in $MAPS; do for l in $LEVELS; do
    jobs_list+=("${m%%:*}-${l%%:*} ${m#*:} ${l#*:}")
done; done
printf '%s\n' "${jobs_list[@]}" | xargs -P "$JOBS" -L 1 bash -c 'run_one "$@"' _

echo
printf '%-15s %s\n' "match" "orig/ours: structures at 10, 20, 30 min; vehicles, infantry, miners at 30 min | largest difference"
compared=0
for entry in "${jobs_list[@]}"; do
    name=${entry%% *}
    rm -f "$OUT/$name.txt"
    orig=$ORACLE_CENSUS/$name/census.log
    if [ ! -s "$orig" ] || ! grep -q '^CENSUS ' "$orig"; then
        printf '%-15s %s\n' "$name" "no original log yet"
        continue
    fi
    if ! grep -q '^CENSUS ' "$OUT/$name.log"; then
        printf '%-15s %s\n' "$name" "our match wrote no census (see $OUT/$name.log)"
        continue
    fi
    python3 tools/compare_census.py "$orig" "$OUT/$name.log" >"$OUT/$name.txt"
    printf '%-15s %s\n' "$name" "$(python3 tools/compare_census.py --brief "$orig" "$OUT/$name.log")"
    compared=$((compared + 1))
done

for entry in "${jobs_list[@]}"; do
    name=${entry%% *}
    [ -s "$OUT/$name.txt" ] || continue
    echo
    echo "== $name: largest differences (full report: $OUT/$name.txt)"
    sed -n '/^Largest differences first:/,$p' "$OUT/$name.txt" | sed -n "2,$((TOP + 1))p"
done
echo
echo "compared $compared matches with the original"
