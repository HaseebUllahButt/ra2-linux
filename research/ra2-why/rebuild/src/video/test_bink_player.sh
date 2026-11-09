#!/usr/bin/env bash
set -eu
# Usage: test_bink_player.sh [game_dir]. Needs the FFmpeg dev libraries
# (libavformat, libavcodec, libswscale, libswresample, libavutil, version 6.x
# works). Found through pkg-config, or set FFMPEG_PREFIX to a staged prefix,
# e.g. FFMPEG_PREFIX=/tmp/ffmpeg-local/root/usr.
script_dir=$(cd "$(dirname "$0")" && pwd)
game_dir=${1:-/path/to/user/ra2/game}
if [[ "$game_dir" != /* ]]; then game_dir="$PWD/$game_dir"; fi
cd "$script_dir"

flags=""
if pkg-config --exists libavformat libavcodec libswscale libswresample libavutil 2>/dev/null; then
    flags=$(pkg-config --cflags --libs libavformat libavcodec libswscale libswresample libavutil)
fi
if [[ -z "$flags" ]]; then
    prefix=${FFMPEG_PREFIX:-/tmp/ffmpeg-local/root/usr}
    if [[ -n "$(find "$prefix" -name 'libavformat.so*' 2>/dev/null | head -1)" ]]; then
        inc=$(dirname "$(find "$prefix" -path '*libavformat/avformat.h' 2>/dev/null | head -1)")
        inc=$(dirname "$inc")
        libdir=$(dirname "$(find "$prefix" -name 'libavformat.so*' 2>/dev/null | head -1)")
        flags="-I $inc -L $libdir -Wl,-rpath,$libdir -lavformat -lavcodec -lswscale -lswresample -lavutil"
    else
        echo "FFmpeg dev libraries not found." >&2
        echo "Install them (sudo apt install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev)" >&2
        echo "or stage them without root into \$FFMPEG_PREFIX (see the report in the git log)." >&2
        exit 1
    fi
fi

binary=$(mktemp /tmp/ra2-bink-player-test.XXXXXX)
trap 'rm -f "$binary"' EXIT
${CXX:-g++} -std=c++20 -Wall -Wextra -Werror -O1 -I .. \
    bink_player.cpp test_bink_player.cpp \
    ../fs/mix_archive.cpp ../fs/mix_bootstrap.cpp ../fs/filename_hash.cpp \
    $flags -o "$binary"
"$binary" "$game_dir"
