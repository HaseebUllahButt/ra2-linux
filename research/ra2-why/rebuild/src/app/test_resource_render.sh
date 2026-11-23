#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
output=$(mktemp /tmp/ra2-resource-test.XXXXXX)
trap 'rm -f "$output"' EXIT
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections -Isrc \
 src/app/test_resource_render.cpp src/app/resource_render.cpp src/actors/actors.cpp \
 src/gfx/shp_decode.cpp src/gfx/palette.cpp src/fs/mix_archive.cpp src/fs/mix_bootstrap.cpp \
 src/fs/filename_hash.cpp src/rules/ini_file.cpp src/pack/map_pack.cpp \
 -Wl,--gc-sections -lcrypto -llzo2 -o "$output"
"$output" "${1:-../game-full}"
