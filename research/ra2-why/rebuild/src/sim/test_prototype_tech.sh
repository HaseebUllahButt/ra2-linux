#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-tech-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I .. prototype_tech.cpp production_runtime.cpp ../rules/ini_file.cpp test_prototype_tech.cpp -Wl,--gc-sections -o "$binary"
"$binary" "${1:-/tmp/ra2dump/RULES.INI}"
