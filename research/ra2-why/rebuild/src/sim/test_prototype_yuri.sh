#!/usr/bin/env bash
set -eu
script_dir=$(cd "$(dirname "$0")" && pwd)
rules=${1:-/path/to/user/ra2-test-host/analysis/extracted/config/rulesmd.ini}
cd "$script_dir"
binary=$(mktemp /tmp/ra2-prototype-yuri.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -Wall -Wextra -Werror -O1 ${SIM_TEST_FLAGS:-} -I .. \
    prototype_yuri.cpp test_prototype_yuri.cpp ../rules/ini_file.cpp -o "$binary"
"$binary" "$rules" ${2:-}
