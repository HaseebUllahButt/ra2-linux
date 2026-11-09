#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
game_dir=${RA2_GAME_DIR:-"$root/game"}
exec "$root/rebuild/build/ra2-native" --game-dir "$game_dir" --skirmish "$@"
