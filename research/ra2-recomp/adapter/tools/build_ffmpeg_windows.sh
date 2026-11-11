#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
zig_bin=${ZIG:-$HOME/.local/bin/zig}
mkdir -p "$root/.reference" "$root/build/ffmpeg-win"
archive="$root/.reference/ffmpeg.tar.xz"
if [[ ! -f "$archive" ]]; then curl -fL --retry 2 https://ffmpeg.org/releases/ffmpeg-8.0.1.tar.xz -o "$archive"; fi
printf '%s  %s\n' 05ee0b03119b45c0bdb4df654b96802e909e0a752f72e4fe3794f487229e5a41 "$archive" | sha256sum -c -
tar -xf "$archive" -C "$root/.reference"
cd "$root/build/ffmpeg-win"
nice -n 19 "$root/.reference/ffmpeg-8.0.1/configure" --prefix="$PWD/install" --target-os=mingw32 --arch=x86 \
    --enable-cross-compile --cc="$root/../tools/wcc" --ar="$zig_bin ar" --ranlib="$zig_bin ranlib" \
    --disable-everything --disable-programs --disable-doc --disable-x86asm --disable-network --disable-autodetect --disable-w32threads \
    --enable-avformat --enable-avcodec --enable-avutil --enable-swscale --enable-swresample \
    --enable-demuxer=bink --enable-decoder=bink,binkaudio_rdft,binkaudio_dct --enable-protocol=file
nice -n 19 make -j4
nice -n 19 make install
