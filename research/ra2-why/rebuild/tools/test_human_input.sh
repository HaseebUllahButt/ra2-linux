#!/usr/bin/env bash
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
binary=${1:-"$root/build/ra2-native"}
game=${2:-"$root/../game"}
captures=$(mktemp -d /tmp/ra2-human-input.XXXXXX)
printf 'Input check captures: %s\n' "$captures"
if ! RA2_MATCH_CAPTURE="$captures/play.ppm" "$binary" --game-dir "$game" \
    --headless --human-input-check --frames 20000 >"$captures/run.log" 2>&1; then
    cat "$captures/run.log"
    exit 1
fi
cat "$captures/run.log"
python3 - "$captures" <<'PY'
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
names = ['00-help', '01-deployed', '02-production', '03-ready', '04-placed',
         '04b-options', '04c-abort-question',
         '05-selected', '06-moved', '07-attack', '08-result',
         '09-camera-after-result', '10-menu']
frames = {}
for name in names:
    with (root / ('play-' + name + '.ppm')).open('rb') as stream:
        assert stream.readline().strip() == b'P6', name
        width, height = map(int, stream.readline().split())
        assert stream.readline().strip() == b'255', name
        pixels = stream.read()
    assert (width, height) == ((800, 600) if name == '10-menu' else (1024, 768)), name
    assert len(pixels) == width * height * 3, name
    colors = set(zip(pixels[example.invalid], pixels[example.invalid], pixels[example.invalid]))
    assert len(colors) > 64, 'Blank capture: ' + name
    frames[name] = pixels

for before, after in [('00-help', '01-deployed'), ('02-production', '03-ready'),
                      ('04-placed', '04b-options'), ('04b-options', '04c-abort-question'),
                      ('03-ready', '04-placed'), ('05-selected', '06-moved'),
                      ('07-attack', '08-result'), ('08-result', '09-camera-after-result')]:
    assert frames[before] != frames[after], 'Unchanged capture: ' + after
log = (root / 'run.log').read_text()
for name in names[:-1]:
    assert 'Human input checkpoint: ' + name in log, name
assert 'world=PrototypeComplete' in log
assert 'Human input result: ' in log and ('result=VICTORY' in log or 'result=DEFEAT' in log), 'Match did not reach a result'
assert 'Native base RA2 menu: 6 frames presented.' in log
print('Human SDL input, result, camera, menu, and capture checks passed.')
PY
