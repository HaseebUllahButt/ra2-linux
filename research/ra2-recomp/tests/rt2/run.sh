#!/bin/sh
# J6 check: runtime v2 under Wine: nested native->lifted calls and lifted code on 5 threads.
set -e; cd "$(dirname "$0")"; W=../../tools/wcc; RT=../../rt
F="-O1 -fwrapv -fno-strict-aliasing -I$RT"
$W $F -shared -o rt2.dll rt2.c orig.S $RT/rc_rt.c $RT/bridge.S ../../shim/rc_win.c
$W -O1 -o rt2main.exe main.c
RC_ON=all RC_LOG=rt2.log WINEPREFIX=${WINEPREFIX:-/path/to/user/Projects/ra2/ra2-recomp/build/prefix} WINEDEBUG=-all wine rt2main.exe
cat rt2.log | head -3
