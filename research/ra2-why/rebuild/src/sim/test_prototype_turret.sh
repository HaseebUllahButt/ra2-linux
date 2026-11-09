#!/usr/bin/env bash
set -eu
# Usage: test_prototype_turret.sh. No game assets are needed.
script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-turret-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -Wall -Wextra -Werror -O1 -I .. \
    prototype_turret.cpp test_prototype_turret.cpp -o "$binary"
"$binary"
