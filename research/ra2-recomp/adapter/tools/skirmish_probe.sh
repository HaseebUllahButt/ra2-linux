#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root/build/game"
export WINEDLLOVERRIDES="${ADAPTER_DLL_OVERRIDES:-ddraw=n};winepulse.drv,winealsa.drv=d"
export RA2_NATIVE_MENU_REPAINT=1
export RA2_AUDIO_CAPTURE='/path/to/user\Projects\ra2\ra2-recomp\adapter\build\game-audio.pcm'
timeout -k 3s 180s wine game.exe > ../skirmish-probe.log 2>&1 &
game_pid=$!
for attempt in $(seq 45); do
    sleep 2
    import -window root ../skirmish-current.png
    difference=$(compare -metric AE -fuzz 12% -crop 160x380+750+280 "$root/../build/menu/ref.png" ../skirmish-current.png null: 2>&1 || true)
    difference=${difference%% *}
    if [[ "$difference" =~ ^[0-9]+$ ]] && (( difference<3000 )); then break; fi
    "$root/tools/xinput.py" key Escape
done
cp ../skirmish-current.png ../skirmish-menu.png
printf 'Menu difference: %s\n' "$difference"
[[ "$difference" =~ ^[0-9]+$ ]] && (( difference<3000 )) || exit 1
"$root/tools/xinput.py" click 830 304
sleep 3
"$root/tools/xinput.py" click 830 388
sleep 3
import -window root ../skirmish-setup.png
"$root/tools/xinput.py" click 830 346
sleep 20
import -window root ../skirmish-battle.png
wait "$game_pid"
