#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
binary=$(mktemp /tmp/ra2-veteran-damage.XXXXXX)
trap 'rm -f "$binary"' EXIT
cd "$script_dir"
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -I .. \
    test_veteran_damage.cpp prototype_special.cpp ../weapons/techno_damage.cpp \
    ../weapons/projectile_damage.cpp ../sim/random.cpp ../rules/ini_file.cpp -o "$binary"
"$binary"
