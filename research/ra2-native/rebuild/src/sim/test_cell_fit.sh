#!/usr/bin/env bash
set -eu
# Usage: test_cell_fit.sh [RULES_INI] [--vectors].
# SIM_CELL_FIT_RULES overrides the repository-relative default rules path.
# An explicit RULES_INI takes priority. Resolve relative inputs from the caller's directory.
script_dir=$(cd "$(dirname "$0")" && pwd)
rules_input=${SIM_CELL_FIT_RULES:-"$script_dir/../../../analysis/extracted/config/rulesmd.ini"}
if [ "$#" -gt 0 ] && [ "$1" != --vectors ]; then
    rules_input=$1
    shift
fi
case "$rules_input" in
    /*) ;;
    *) rules_input="$PWD/$rules_input" ;;
esac
cd "$script_dir"
binary=$(mktemp /tmp/ra2-sim-cell-fit-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -O2 ${SIM_TEST_FLAGS:-} \
    cell_fit.cpp ../rules/ini_file.cpp test_cell_fit.cpp -o "$binary"
"$binary" "$rules_input" "$@"
