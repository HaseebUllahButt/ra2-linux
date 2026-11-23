#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
# Keep compiler temporary files and all generated artifacts in the write scope.
export TMPDIR="$PWD/rebuild/src/render"
"${CXX:-g++}" -pipe -std=c++17 -Wall -Wextra -Werror -O2 \
    rebuild/src/render/{tmp_decode,minimap,test_main}.cpp \
    rebuild/src/fs/{mix_archive,mix_bootstrap,filename_hash}.cpp \
    rebuild/src/gfx/palette.cpp rebuild/src/pack/map_pack.cpp \
    rebuild/src/rules/ini_file.cpp -ldl -o rebuild/src/render/render_test
rebuild/src/render/render_test "$@"
