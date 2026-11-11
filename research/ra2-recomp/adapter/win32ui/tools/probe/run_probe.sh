#!/bin/sh
# Run a probe program under Wine on hidden display :82 with a private prefix (build/prefix-probe), muted.
# usage: tools/probe/run_probe.sh <exe> [args...]   env RA2UI=1 to run on our code.
H=$(cd "$(dirname "$0")/../.." && pwd)
[ -d "$H/build/prefix-probe" ] || cp -a "$H/build/prefix" "$H/build/prefix-probe"
export WINEPREFIX=$H/build/prefix-probe DISPLAY=:82 WINEDEBUG=-all WINEPATH="$H/${UIBUILD:-build}" RA2UI_NO_SDL_WINDOW=1
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent SDL_AUDIODRIVER=dummy WINEDLLOVERRIDES="winepulse.drv,winealsa.drv=d"
pgrep -u "$USER" -x Xvfb -a | grep -q ":82 " || { (Xvfb :82 -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 &) ; sleep 2; }
export RA2UI_DLL="Z:$H/${UIBUILD:-build}/ra2ui.dll"
cd "$H/build" && wine "$@"   # probe exes live in build/; UIBUILD picks the ra2ui.dll folder
