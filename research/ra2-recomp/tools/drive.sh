#!/bin/sh
# Run the game with rc.dll on the hidden display and follow a script of steps.
# usage: tools/drive.sh <out-dir> "<steps>"   steps: "w 40; c 832 304; w 5; s name; k 13; ..."
#   w N = wait N s, c X Y = click, m X Y = move, s NAME = screenshot NAME.png
# env: RC_ON, RC_OFF, RC_NOLIFT=1 (start game.exe without rc.dll)
R=$(cd "$(dirname "$0")/.." && pwd)
[ -z "$RC_LOCKED" ] && exec env RC_LOCKED=1 flock -o /tmp/ra2-game-why.lock "$0" "$@"
OUT=$(mkdir -p "$1" && cd "$1" && pwd)
export DISPLAY=:77 WINEPREFIX=$R/build/prefix WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES="ddraw=n,b;winepulse.drv,winealsa.drv=d"
export PULSE_SERVER=unix:/nonexistent PIPEWIRE_REMOTE=/nonexistent ALSA_CARD=none SDL_AUDIODRIVER=dummy
export RC_LOG=$OUT/rc.log
timeout 5 xdpyinfo -display :77 >/dev/null 2>&1 || timeout 5 import -display :77 -window root png:/dev/null 2>/dev/null || { P=$(pgrep -u "$USER" -f "^Xvfb :77"); [ -n "$P" ] && kill -9 $P; rm -f /tmp/.X77-lock; Xvfb :77 -screen 0 1024x768x24 -nolisten tcp >/dev/null 2>&1 & sleep 2; }
wineserver -k 2>/dev/null; sleep 1
cd /path/to/user/Projects/ra2/original/game
if [ -n "$RC_NOLIFT" ]; then nice -n 10 wine game.exe > "$OUT/wine.log" 2>&1 &
else W=${RC_DLLDIR:-$R/build/win}; nice -n 10 wine "$W/rclaunch.exe" game.exe "$W/rc.dll" > "$OUT/wine.log" 2>&1 & fi
sleep 5
if [ "$(pw-cli ls Node 2>/dev/null | grep -c 'Red Alert')" != 0 ]; then echo "SOUND LEAK: killing the game"; wineserver -k; exit 3; fi
echo "$2" | tr ';' '\n' | while read -r op a b; do
  case "$op" in
    w) sleep "$a";;
    c) python3 "$R/tools/xmouse.py" "$a" "$b" click;;
    m) python3 "$R/tools/xmouse.py" "$a" "$b";;
    s) import -window root "$OUT/$a.png";;
  esac
done
wineserver -k 2>/dev/null
grep -v "^t=" "$OUT/rc.log" 2>/dev/null | tail -5; tail -1 "$OUT/rc.log" 2>/dev/null
