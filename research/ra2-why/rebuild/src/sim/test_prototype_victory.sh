#!/usr/bin/env bash
set -eu
# Usage: test_prototype_victory.sh. No game assets are needed.
script_dir=$(cd "$(dirname "$0")" && pwd)
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-victory-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    -I .. prototype_victory.cpp test_prototype_victory.cpp -o "$binary"
"$binary" "$@"
