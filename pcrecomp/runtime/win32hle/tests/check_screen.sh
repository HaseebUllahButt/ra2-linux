#!/bin/sh
# Compile with the target's SDL2 and SDL2_ttf pkg-config paths.
set -eu
cd "$(dirname "$0")"
out=${SCREEN_TEST_OUT:-/tmp/screen_present}
${CC:-cc} ${CFLAGS:-} -O2 -g -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections \
    -I.. -I../../recomp32 screen_present.c $(pkg-config --cflags --libs sdl2 SDL2_ttf) \
    -Wl,--gc-sections -pthread -lm -o "$out"
"$out" "$@"
