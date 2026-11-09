#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-resource-cell-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    resource_cell.cpp test_resource_cell.cpp -o "$binary"
"$binary"
if [ "$#" -gt 0 ]; then
    python3 original_resource_cell_check.py "$1" "$binary"
fi
