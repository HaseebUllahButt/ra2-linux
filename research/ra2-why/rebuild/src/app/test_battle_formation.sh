#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-formation.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -I .. test_battle_formation.cpp -o "$binary"
"$binary"
