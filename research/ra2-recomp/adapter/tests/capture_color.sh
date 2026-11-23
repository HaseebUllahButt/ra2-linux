#!/usr/bin/env bash
set -euo pipefail
adapter_root=$(cd "$(dirname "$0")/.." && pwd)
case "${1:-}" in
    wine) color_override='ddraw=b;winepulse.drv,winealsa.drv=d' ;;
    adapter) color_override='ddraw=n;winepulse.drv,winealsa.drv=d' ;;
    *) echo 'Use capture_color.sh wine|adapter' >&2; exit 1 ;;
esac
export WINEDLLOVERRIDES="$color_override"
color_variant=$1
cd "$adapter_root/build"
coproc color_app { wine ./test_color.exe 2>"color-$color_variant.log"; }
color_app_pid=$color_app_PID
read -r -t 45 color_ready <&"${color_app[0]}"
[[ "${color_ready%$'\r'}" == READY ]]
import -window root "color-$color_variant.png"
wait "$color_app_pid"
echo "Captured $color_variant RGB565 pattern"
