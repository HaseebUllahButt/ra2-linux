#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-walk-motion-test.XXXXXX)
printed=$(mktemp /tmp/ra2-sim-walk-motion-print.XXXXXX)
trap 'rm -f "$binary" "$printed"' EXIT
# One compiler process on purpose: other helpers share this machine.
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    walk_motion.cpp test_walk_motion.cpp -o "$binary"
game=${RA2_GAME_DIR:-$(cd ../../.. && pwd)/../ra2-movement-port/game-full}
"$binary" "$game/game.exe"
"$binary" "$game/game.exe" --print > "$printed"
python3 original_walk_math_check.py "$game/game.exe" "$printed"
