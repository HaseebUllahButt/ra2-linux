#!/usr/bin/env bash
# Benchmark the native skirmish without a visible window.
# It draws every frame, skips the frame limiter, and prints average and worst
# times per phase plus a world hash every 100 frames.
# Usage: tools/bench_skirmish.sh <game-dir> [frames]
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
game=${1:?game folder}
frames=${2:-3000}
RA2_BENCH=1 SDL_VIDEODRIVER=dummy "$here/build/ra2-native" --game-dir "$game" --skirmish \
    --autoplay --frames "$frames" 2>&1 | grep -E '^RA2_BENCH|^Native prototype' || true
