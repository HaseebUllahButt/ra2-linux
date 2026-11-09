#!/usr/bin/env bash
set -eu
# Usage: test_campaign_flow.sh [game-dir]. Real-data part is skipped when the dir is missing.
cd "$(dirname "$0")"
binary=$(mktemp /tmp/ra2-campaign-flow-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -O1 -Wall -Wextra -Werror \
    campaign_flow.cpp test_campaign_flow.cpp ../text/csf.cpp ../rules/ini_file.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp -ldl -o "$binary"
RA2_GAME_DIR="${1:-${RA2_GAME_DIR:-/path/to/user/ra2-test-host/game}}" "$binary"
