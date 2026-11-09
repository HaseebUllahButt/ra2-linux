#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-game-audio.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror -I .. -I ../fs \
 game_audio.cpp audio.cpp lookup.cpp test_game_audio.cpp ../rules/ini_file.cpp \
 ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -ldl -o "$binary"
"$binary" "${1:-/path/to/user/ra2-test-host/game}"
