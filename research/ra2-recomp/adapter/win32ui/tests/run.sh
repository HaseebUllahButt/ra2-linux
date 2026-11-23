#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/ctltest${CTLTEST_VISIBLE:+-visible}"
mkdir -p "$OUT"
cp "$ROOT/build/ctltest.exe" "$ROOT/build/ra2ui-port.dll" "$ROOT/build/SDL2.dll" "$OUT/"
cp "$ROOT/build/ra2ui-port.dll" "$OUT/ra2ui.dll"
export WINEPREFIX="$OUT/prefix" DISPLAY=:81 WINEDEBUG=-all
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent SDL_AUDIODRIVER=dummy
export WINEDLLOVERRIDES='winepulse.drv,winealsa.drv=d'
export RA2UI_NO_SDL_WINDOW=1 RA2UI_LOG="$OUT/ui.log"
if [ ! -d "$WINEPREFIX" ]; then cp -a "$ROOT/build/prefix" "$WINEPREFIX"; fi
Xvfb :81 -screen 0 1024x768x24 -nolisten tcp >"$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'wineserver -k 2>/dev/null || true; kill "$XVFB_PID" 2>/dev/null || true' EXIT
sleep 2
wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d '' /f >"$OUT/reg.log" 2>&1
cd "$OUT"
GAME='/path/to/user\Projects\ra2\original\game\game.exe'
env -u RA2UI timeout 45 wine ./ctltest.exe "$GAME" wine.log >wine.stderr 2>&1
RA2UI=1 timeout 45 wine ./ctltest.exe "$GAME" ours.log >ours.stderr 2>&1
python3 "$ROOT/tests/compare.py" wine.log ours.log
