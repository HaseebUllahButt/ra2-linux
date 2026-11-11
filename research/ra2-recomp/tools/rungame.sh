#!/bin/sh
# Run the original game.exe with rc.dll on a hidden display (Xvfb :77), no sound.
# usage: tools/rungame.sh <out-dir> [seconds=45] [click]   env: RC_ON, RC_OFF (see shim/rc_win.c)
# Writes <out>/rc.log, <out>/shot.png (and click.png), prints MENU OK/FAIL (+ MOUSE OK/FAIL).
R=$(cd "$(dirname "$0")/.." && pwd)
# one game at a time on this machine (shared lock with other helpers)
[ -z "$RC_LOCKED" ] && exec env RC_LOCKED=1 flock /tmp/ra2-game-why.lock "$0" "$@"
OUT=$(mkdir -p "$1" && cd "$1" && pwd); SECS=${2:-45}
export DISPLAY=:77 WINEPREFIX=$R/build/prefix WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES="ddraw=n,b;winepulse.drv,winealsa.drv=d"
export RC_LOG=$OUT/rc.log
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent ALSA_CARD=none SDL_AUDIODRIVER=dummy
pgrep -u "$USER" -f "Xvfb :77" >/dev/null || { Xvfb :77 -screen 0 1024x768x24 >/dev/null 2>&1 & sleep 2; }
wineserver -k 2>/dev/null; sleep 1
cd /path/to/user/Projects/ra2/original/game
nice -n 10 wine "$R/build/win/rclaunch.exe" game.exe "$R/build/win/rc.dll" > "$OUT/wine.log" 2>&1 &
sleep 5
if [ "$(pw-cli ls Node 2>/dev/null | grep -c 'Red Alert')" != 0 ]; then
  echo "SOUND LEAK: killing the game"; wineserver -k; exit 3
fi
sleep $((SECS - 5))
import -window root "$OUT/shot.png"
# menu check: right button column against the reference
d=$(compare -metric AE -fuzz 12% -crop 160x380+750+280 "$R/build/menu/ref.png" "$OUT/shot.png" null: 2>&1 | cut -d' ' -f1)
if [ "${d%.*}" -lt 3000 ] 2>/dev/null; then echo "MENU OK (diff $d)"; else echo "MENU FAIL (diff $d)"; fi
if [ "$3" = click ]; then
  python3 "$R/tools/xmouse.py" 832 472 click; sleep 6     # Options
  import -window root "$OUT/click.png"
  e=$(compare -metric AE -fuzz 12% "$OUT/shot.png" "$OUT/click.png" null: 2>&1 | cut -d' ' -f1)
  if [ "${e%.*}" -gt 20000 ] 2>/dev/null; then echo "MOUSE OK (screen changed $e)"; else echo "MOUSE FAIL (change $e)"; fi
fi
wineserver -k 2>/dev/null
tail -3 "$OUT/rc.log" 2>/dev/null
