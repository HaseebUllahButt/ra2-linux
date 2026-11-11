#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
binary=$(mktemp /tmp/ra2-crate-damage.XXXXXX)
trap 'rm -f "$binary"' EXIT
cd "$script_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -I .. \
    test_crate_damage.cpp prototype_special.cpp random.cpp \
    ../rules/ini_file.cpp -o "$binary"
"$binary"
