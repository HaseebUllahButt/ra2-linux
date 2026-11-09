#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d /tmp/ra2-battle-saves.XXXXXX)
trap 'rm -rf "$build"' EXIT
${CXX:-c++} -std=c++20 -O1 -Wall -Wextra -Werror -I .. battle_saves.cpp test_battle_saves.cpp -o "$build/test_battle_saves"
"$build/test_battle_saves" "$build"
