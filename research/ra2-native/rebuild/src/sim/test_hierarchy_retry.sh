#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-sim-hierarchy-retry-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    retry_edges.cpp hierarchy_retry.cpp test_hierarchy_retry.cpp -o "$binary"
"$binary" "$@"
