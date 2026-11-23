#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-category-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} coarse_category.cpp ../rules/ini_file.cpp test_category.cpp -o "$binary"
"$binary" "${1:-/path/to/user/ra2/analysis/extracted/config/rulesmd.ini}"
