#!/bin/sh
# Instruction fuzzer: every instruction form in game.exe, real bytes against lifted C.
# usage: tools/insnfuzz.sh [harness options: -n states, -p 53|64|both, -f filter, -s seed, -v]
#   INSNFUZZ_COLLECT=1 collects the forms again (3 minutes); else uses tests/fuzz/forms.json.
# Output: build/fuzz/run.log; the last line says how many mismatches.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B=$ROOT/build/fuzz
mkdir -p "$B"; touch "$ROOT/build/.trackerignore" "$B/.trackerignore"
N="nice -n 19"
if [ -n "$INSNFUZZ_COLLECT" ] || [ ! -f "$ROOT/tests/fuzz/forms.json" ]; then
  $N python3 "$ROOT/tools/insnfuzz.py" collect "$ROOT/tests/fuzz/forms.json"
fi
$N python3 "$ROOT/tools/insnfuzz.py" gen "$ROOT/tests/fuzz/forms.json" "$B"
CF="-m32 -O1 -std=gnu11 -fno-strict-aliasing -fwrapv -w -mfpmath=387 -frounding-math -fno-pie -no-pie -I$ROOT/rt -I$ROOT/tests/fuzz"
# lifted code: same flags as tools/mktest.py; two jobs at a time
for i in 0 1 2 3; do
  $N gcc $CF -c "$B/gen$i.c" -o "$B/gen$i.o" &
  if [ $i = 1 ] || [ $i = 3 ]; then wait; fi
done
$N gcc $CF -c "$B/table.c" -o "$B/table.o"
$N gcc $CF -O1 -o "$B/harness" "$ROOT/tests/fuzz/harness.c" "$ROOT/tests/fuzz/native.S" "$B"/gen?.o "$B/table.o"
echo "skipped forms (cannot run alone or lifter rejects): $(wc -l < "$B/skipped.txt")  see $B/skipped.txt"
set +e
$N "$B/harness" "$@" > "$B/run.log"
rc=$?
grep -c '^  MISMATCH' "$B/run.log" | sed 's/^/example runs with mismatches: /'
tail -1 "$B/run.log"
exit $rc
