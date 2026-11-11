#!/usr/bin/env bash
set -euo pipefail
adapter_root=$(cd "$(dirname "$0")/.." && pwd)
xvfb_bin=${XVFB_BIN:-$HOME/.local/bin/Xvfb}
if [[ ! -x "$xvfb_bin" ]]; then
    if command -v Xvfb >/dev/null 2>&1; then
        xvfb_bin=$(command -v Xvfb)
    else
        xvfb_bin="$adapter_root/.reference/xvfb/usr/bin/Xvfb"
    fi
    [[ -x "$xvfb_bin" ]] || { echo 'Install Xvfb or set XVFB_BIN.' >&2; exit 1; }
fi
coproc adapter_xserver { exec "$xvfb_bin" -displayfd 1 -screen 0 1024x768x24 -nolisten tcp -noreset 2>"$adapter_root/build/xvfb.log"; }
adapter_xserver_pid=$adapter_xserver_PID
cleanup() {
    if [[ "${WINEPREFIX:-}" == "$adapter_root/build/prefix-hidden" ]]; then
        wineserver -k 2>/dev/null || true
        timeout 10s wineserver -w 2>/dev/null || true
    fi
    kill "$adapter_xserver_pid" 2>/dev/null || true
    wait "$adapter_xserver_pid" 2>/dev/null || true
}
trap cleanup EXIT
read -r -t 15 adapter_display <&"${adapter_xserver[0]}"
export DISPLAY=":$adapter_display"
export WINEPREFIX="${ADAPTER_WINEPREFIX:-$adapter_root/build/prefix-hidden}"
export WINEDEBUG=-all
export WINEDLLOVERRIDES="ddraw=n;winepulse.drv,winealsa.drv=d"
export SDL_AUDIODRIVER=dummy
printf 'Hidden display: %s\n' "$DISPLAY" >&2
"$@"
