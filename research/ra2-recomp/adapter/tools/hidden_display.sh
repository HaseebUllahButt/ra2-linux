#!/usr/bin/env bash
set -euo pipefail
adapter_root=$(cd "$(dirname "$0")/.." && pwd)
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent ALSA_CARD=none SDL_AUDIODRIVER=dummy
export WINEPREFIX="${ADAPTER_WINEPREFIX:-$adapter_root/build/prefix-hidden}"
case "$WINEPREFIX" in "$adapter_root"/build/*) ;; *) echo 'Use a private prefix under adapter/build.' >&2; exit 1;; esac
xvfb_bin=${XVFB_BIN:-$HOME/.local/bin/Xvfb}
if [[ ! -x "$xvfb_bin" ]]; then xvfb_bin=$(command -v Xvfb); fi
coproc adapter_xserver { exec "$xvfb_bin" -displayfd 1 -screen 0 1024x768x24 -nolisten tcp -noreset 2>"$adapter_root/build/xvfb.log"; }
adapter_xserver_pid=$adapter_xserver_PID
adapter_task_pid=
cleanup() {
    wineserver -k 2>/dev/null || true
    timeout 10s wineserver -w 2>/dev/null || true
    if [[ -n "$adapter_task_pid" ]]; then kill "$adapter_task_pid" 2>/dev/null || true; fi
    kill "$adapter_xserver_pid" 2>/dev/null || true
    wait "$adapter_xserver_pid" 2>/dev/null || true
}
trap cleanup EXIT
read -r -t 15 adapter_display <&"${adapter_xserver[0]}"
export DISPLAY=":$adapter_display" WINEDEBUG=-all
export WINEDLLOVERRIDES="ddraw=n;winepulse.drv,winealsa.drv=d"
printf 'Hidden display: %s\n' "$DISPLAY" >&2
wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d '' /f >"$adapter_root/build/audio-disable.log" 2>&1
# Wine 11 filters Unix SDL_ variables. Set the Windows environment as well.
wine reg add 'HKCU\Environment' /v SDL_AUDIODRIVER /t REG_SZ /d dummy /f >>"$adapter_root/build/audio-disable.log" 2>&1
"$@" &
adapter_task_pid=$!
sleep 5
# Query the owner's PipeWire server, not the blocked test connection.
if ! adapter_nodes=$(env -u PIPEWIRE_REMOTE -u PULSE_SERVER pw-cli ls Node 2>&1); then
    echo 'Audio guard cannot inspect PipeWire. Test stopped.' >&2; exit 1
fi
adapter_stream_count=$(printf '%s\n' "$adapter_nodes" | grep -c 'Red Alert' || true)
printf 'Audio guard: Red Alert nodes=%s\n' "$adapter_stream_count" >&2
if [[ "$adapter_stream_count" != 0 ]]; then echo 'Audio leak detected. Test stopped.' >&2; exit 1; fi
wait "$adapter_task_pid"
