#!/bin/sh
# Build and run every test group against the whole lifted game. One group at a time.
# usage: tools/run_groups.sh   (results in build/<group>/difftest.log, summary in reports/groups.txt)
cd "$(dirname "$0")/../build" || exit 1
T=/path/to/user/Projects/ra2/ra2-1to1
EXE=$T/inputs/game.exe
: > ../reports/groups.txt.new
ONLY="$*"
while read -r name fixture addrs; do
  [ -z "$name" ] && continue
  case "$name" in \#*) continue;; esac
  if [ -n "$ONLY" ]; then case " $ONLY " in *" $name "*) ;; *) continue;; esac; fi
  fx=-; [ "$fixture" != "-" ] && fx=$T/fixtures/$fixture
  python3 ../tools/mktest.py "$name" "$fx" -1 $addrs > "$name.build.log" 2>&1 || { echo "$name BUILD FAILED" >> ../reports/groups.txt.new; continue; }
  (cd "$name" && DIFF_STRICT=1 timeout 1800 nice ./test "$EXE" > run.log 2>&1)
  echo "== $name ($fixture)" >> ../reports/groups.txt.new
  grep -E "CANNOT|NO TEST" "$name.build.log" >> ../reports/groups.txt.new
  grep -v " set[0-9]" "$name/difftest.log" >> ../reports/groups.txt.new
  tail -1 "$name/run.log" >> ../reports/groups.txt.new
done < ../tests/groups.txt
cat ../reports/groups.txt.new >> ../reports/groups.txt; rm ../reports/groups.txt.new
