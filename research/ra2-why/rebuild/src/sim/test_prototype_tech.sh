#!/usr/bin/env bash
set -eu
# Usage: test_prototype_tech.sh [base-game-directory-or-RULES.INI].
# A directory resolves to its RULES.INI. Relative paths resolve from the
# caller's directory. Without an argument only the pure checks run.
game_input=${1:-}
if [ -n "$game_input" ]; then
    case "$game_input" in
        /*) ;;
        *) game_input="$PWD/$game_input" ;;
    esac
fi
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-tech-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -I .. prototype_tech.cpp production_runtime.cpp ../rules/ini_file.cpp test_prototype_tech.cpp -Wl,--gc-sections -o "$binary"
"$binary" ${game_input:+"$game_input"}
