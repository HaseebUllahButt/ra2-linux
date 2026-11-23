#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .reference
archive=.reference/SDL2-mingw.tar.gz
if [ ! -f "$archive" ]; then
    curl -fL --retry 2 https://libsdl.org/release/SDL2-devel-2.30.12-mingw.tar.gz -o "$archive"
fi
printf '%s  %s\n' dddafbf0705a8cbe1f97c69680886ce147ed4d38f1d04e03523681298e081b0f "$archive" | sha256sum -c -
tar -xzf "$archive" -C .reference
