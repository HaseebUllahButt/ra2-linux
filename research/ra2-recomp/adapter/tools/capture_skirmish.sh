#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
variant=${1:?Use adapter or wine}
case "$variant" in adapter) draw_override=n;; wine) draw_override=b;; *) exit 2;; esac
export WINEDLLOVERRIDES="ddraw=$draw_override;dsound=n;winepulse.drv,winealsa.drv=d"
export RA2_NATIVE_MENU_REPAINT=${RA2_NATIVE_MENU_REPAINT:-0}
ui_dll=${ADAPTER_UI_DLL:-$root/win32ui/build/ra2ui.dll}
export RA2UI_ALSO_LOAD=$(python3 -c 'import sys; print("Z:"+sys.argv[1].replace("/",chr(92)))' "$root/build/capture_ddraw.dll")
export RA2UI_LOG=$(python3 -c 'import sys; print("Z:"+sys.argv[1].replace("/",chr(92)))' "$root/build/capture-$variant-ui.log")
export ADAPTER_CAPTURE_DIR=$(python3 -c 'import sys; print("Z:"+sys.argv[1].replace("/",chr(92)))' "$root/build/captures-$variant")
export RA2_AUDIO_CAPTURE=$(python3 -c 'import sys; print("Z:"+sys.argv[1].replace("/",chr(92)))' "$root/build/game-$variant-audio.pcm")
mkdir -p "$root/build/captures-$variant"
rm -f "$root/build/captures-$variant"/frame_*.rgb565 "$root/build/captures-$variant/observer.log"
python3 - "$root/build/game/RA2.INI" <<'PY'
import configparser
import sys
p=sys.argv[1]; c=configparser.ConfigParser(); c.optionxform=str; c.read(p)
for key,value in {'ScenIndex':'0','GameSpeed':'3','Credits':'10000','UnitCount':'10','ShortGame':'no','SuperWeaponsAllowed':'no','CratesAppear':'no','Slot01':'2,5,1','Slot02':'1,-2,-2','Slot03':'1,-2,-2','Slot04':'1,-2,-2','Slot05':'1,-2,-2','Slot06':'1,-2,-2','Slot07':'1,-2,-2'}.items(): c['Skirmish'][key]=value
c['MultiPlayer']['Side']='Americans'
with open(p,'w') as f: c.write(f,space_around_delimiters=False)
PY
cd "$root/build/game"
timeout -k 3s 240s wine ../capture_launch.exe game.exe "$ui_dll" >"../capture-$variant-game.log" 2>&1 &
game_pid=$!
# The observer sends the same button notifications after each dialog is ready.
wait "$game_pid"
