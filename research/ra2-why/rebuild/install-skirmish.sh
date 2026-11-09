#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
game_dir=$(realpath "${1:-$root/game}")
python3 - "$root" "$game_dir" <<'PY'
import pathlib
import shlex
import sys

root, game = map(pathlib.Path, sys.argv[1:])
binary = root / 'rebuild/build/ra2-native'
if not binary.is_file() or not game.is_dir():
    raise SystemExit('Build the native client and supply the owned game folder first.')
home = pathlib.Path.home()
launcher = home / '.local/bin/ra2-native-prototype-skirmish'
entry = home / '.local/share/applications/ra2-native-prototype-skirmish.desktop'
wrapper = ('#!/usr/bin/env bash\nset -eu\nexec ' + shlex.quote(str(binary)) +
           ' --game-dir ' + shlex.quote(str(game)) + ' "$@"\n')
quoted_launcher = str(launcher).replace('\\', '\\\\').replace('"', '\\"')
desktop = ('[Desktop Entry]\nType=Application\nName=RA2 Native Skirmish (Prototype)\n'
           f'Exec="{quoted_launcher}"\nPath={root}\nIcon=applications-games\n'
           'Terminal=false\nCategories=Game;StrategyGame;\nStartupNotify=true\n')
outputs = ((launcher, wrapper), (entry, desktop))
for path, content in outputs:
    if path.exists() and path.read_text() != content:
        raise SystemExit(f'Existing launcher left unchanged: {path}')
for path, content in outputs:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        with path.open('x') as stream:
            stream.write(content)
launcher.chmod(0o755)
print('Installed RA2 Native Skirmish (Prototype).')
print(launcher)
PY
