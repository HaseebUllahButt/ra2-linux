#!/bin/sh
# J1 check: build hello.exe + hellodll.dll and run under the original Wine prefix.
set -e; cd "$(dirname "$0")"; W=../../tools/wcc
$W -O2 -o hello.exe hello.c && $W -O2 -shared -o hellodll.dll hellodll.c
WINEPREFIX=/path/to/user/Projects/ra2/original/prefix WINEDEBUG=-all wine hello.exe
