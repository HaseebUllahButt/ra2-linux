#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
make windows
cd build
../tools/hidden_display.sh timeout 45s wine ./test_ddraw.exe
