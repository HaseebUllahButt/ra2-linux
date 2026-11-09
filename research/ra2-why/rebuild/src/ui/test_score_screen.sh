#!/usr/bin/env bash
set -eu
# Usage: test_score_screen.sh [game_directory]. Uses real game files.
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-score-screen-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror \
    score_screen.cpp ui.cpp shell.cpp skirmish.cpp test_score_screen.cpp \
    ../gfx/shp_decode.cpp ../gfx/palette.cpp ../text/csf.cpp ../rules/ini_file.cpp \
    ../fs/filename_hash.cpp ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp \
    -I .. -o "$binary"
"$binary" "${1:-/path/to/user/ra2-test-host/game}"
