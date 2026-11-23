#!/bin/sh
# Build and run the typed x87 / jump table / virtual call tests against the whole lifted game.
cd "$(dirname "$0")/../build" || exit 1
T=/path/to/user/Projects/ra2/ra2-1to1
mkdir -p special && cp pass-a/glue.c special/ && cd special || exit 1
ls ../all/lift/*.o > objs.txt
nice gcc -m32 -std=gnu11 -O1 -g -fno-strict-aliasing -fwrapv -w -fno-pie -no-pie -mfpmath=387 -frounding-math \
  -I../../rt -I$T/src ../../rt/special_main.c $T/runtime/pilot_rt.c $T/runtime/hybrid.c $T/runtime/call_orig.c \
  $T/runtime/win32_stubs.c ../../rt/rc_rt.c ../../rt/bridge.S glue.c @objs.txt -lm -o special || exit 1
./special $T/inputs/game.exe 2>&1 | grep -v "^\[map\|hybrid" > ../../reports/special.txt
tail -1 ../../reports/special.txt
