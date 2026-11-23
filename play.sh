#!/bin/sh
set -eu

base=$(dirname -- "$(readlink -f -- "$0")")
if [ ! -x "$base/ra2" ]; then
    base="${XDG_DATA_HOME:-$HOME/.local/share}/ra2-linux"
fi
if [ ! -x "$base/ra2" ]; then
    printf 'Run ./install.sh to build and install Red Alert 2.\n' >&2
    exit 1
fi
exec "$base/ra2" --run --game "$base/game" "$@"
