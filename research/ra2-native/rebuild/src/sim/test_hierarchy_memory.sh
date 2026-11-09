#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-hierarchy-memory-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    regions.cpp hierarchy.cpp hierarchy_route.cpp search_memory.cpp test_hierarchy_memory.cpp -o "$binary"
"$binary" "$@"
