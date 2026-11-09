#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d /tmp/ra2-battle-help.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -I .. battle_help.cpp test_battle_help.cpp -o "$build/test_battle_help"
"$build/test_battle_help"
