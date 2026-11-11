#!/bin/sh
# Run the original game.exe with ra2ui.dll on a hidden display, muted, and follow steps.
# usage: tools/run.sh <out-dir> "<steps>"
#   steps: "w 40; c 832 304; m X Y; s name; k KEYSYM; ..." (w wait s, c click, m move, s screenshot, k key)
# env: UI=0 (no ra2ui.dll, real Wine user32), DDRAW=b (Wine ddraw, default) | n (game folder ddraw.dll)
#      LIFT=1 (also load rc.dll with RC_ON), RA2UI_TRACE=1, WDEBUG (WINEDEBUG channels)
# Own Wine prefix (build/prefix, a copy of ../../build/prefix) and own hidden display, so other helpers'
# games are not touched. SLOT=0..2 picks display :78+SLOT and prefix build/prefix<SLOT> (one game per slot).
H=$(cd "$(dirname "$0")/.." && pwd); R=$(cd "$H/../.." && pwd)
SLOT=${SLOT:-0}; PFX=$H/build/prefix; [ "$SLOT" != 0 ] && PFX=$H/build/prefix$SLOT
[ -d "$PFX" ] || cp -a "$H/build/prefix" "$PFX"
[ -z "$UI_LOCKED" ] && exec env UI_LOCKED=1 flock /tmp/ra2ui-slot$SLOT.lock "$0" "$@"
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
DNUM=$((78 + SLOT))
export DISPLAY=:$DNUM WINEPREFIX=$PFX WINEDEBUG=${WDEBUG:--all}
export WINEDLLOVERRIDES="ddraw=${DDRAW:-b};winepulse.drv,winealsa.drv=d"
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent ALSA_CARD=none SDL_AUDIODRIVER=dummy
export RA2UI_LOG=$OUT/ui.log RC_LOG=$OUT/rc.log WINEPATH="$H/build"
pgrep -u "$USER" -f "Xvfb :$DNUM" >/dev/null || { Xvfb :$DNUM -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & sleep 2; }
wineserver -k 2>/dev/null; sleep 1
wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /t REG_SZ /d '' /f >/dev/null 2>&1
cd /path/to/user/Projects/ra2/original/game
if [ "${UI:-1}" = 0 ]; then
  if [ -n "$LIFT" ]; then nice -n 10 wine "$R/build/win/rclaunch.exe" game.exe "$R/build/win/rc.dll" > "$OUT/wine.log" 2>&1 &
  else nice -n 10 wine game.exe > "$OUT/wine.log" 2>&1 & fi
  GPID=$!
else
  [ -n "$LIFT" ] && export RA2UI_ALSO_LOAD="$(winepath -w "$R/build/win/rc.dll")"
  nice -n 10 wine "$R/build/win/rclaunch.exe" game.exe "$H/build/ra2ui.dll" > "$OUT/wine.log" 2>&1 &
  GPID=$!
fi
sleep 5
if [ "$(env -u PIPEWIRE_REMOTE -u PULSE_SERVER pw-cli ls Node 2>/dev/null | grep -c 'Red Alert')" != 0 ]; then echo "SOUND LEAK: killing the game"; wineserver -k; exit 3; fi
echo "$2" | tr ';' '\n' | while read -r op a b; do
  case "$op" in
    w) sleep "$a";;
    c) python3 "$H/tools/xmouse.py" "$a" "$b" click;;
    m) python3 "$H/tools/xmouse.py" "$a" "$b";;
    s) import -window root "$OUT/$a.png";;
    k) python3 "$H/tools/xmouse.py" key "$a";;
    r) python3 "$H/tools/xmouse.py" "$a" "$b" rclick;;
  esac
done
kill -0 "$GPID" 2>/dev/null && echo "game still running at end" || echo "game exited"
wineserver -k 2>/dev/null
tail -5 "$OUT/ui.log" 2>/dev/null
