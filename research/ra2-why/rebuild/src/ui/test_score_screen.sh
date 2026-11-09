#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-score-screen-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 \
    score_screen.cpp test_score_screen.cpp \
    ../gfx/shp_decode.cpp ../gfx/palette.cpp \
    ../fs/filename_hash.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp \
    -I .. -o "$binary"
"$binary" "${1:-/path/to/user/ra2/game}"
