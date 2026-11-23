#!/bin/bash
# Build build/orders.dll (the original game's hook) with clang + lld-link and Wine's import libraries.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$here/build"
clang --target=i686-w64-windows-gnu -O2 -fms-extensions -I/usr/include/wine/windows -isystem /usr/include/wine/msvcrt \
  -c "$here/src/orders_dll.c" -o "$here/build/orders_dll.o"
lld-link /dll /safeseh:no /entry:DllMain@12 /out:"$here/build/orders.dll" "$here/build/orders_dll.o" \
  /usr/lib/wine/i386-windows/libkernel32.a /usr/lib/wine/i386-windows/libucrtbase.a
sha256sum "$here/build/orders.dll"
