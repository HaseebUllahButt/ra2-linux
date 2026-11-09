#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
exec "$root/rebuild/build/ra2-native" --game-dir "$root/game" --skirmish "$@"
