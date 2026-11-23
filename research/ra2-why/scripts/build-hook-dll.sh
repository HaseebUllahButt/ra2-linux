#!/bin/sh
# Cross-compile the phase-3 hook DLL (32-bit Windows PE) on this
# ARM64 Linux box, no root needed. The mingw toolchain lives in
# ~/mingw-root (extracted from Ubuntu .debs; see README in rebuild/hook).
set -e
export PATH="$HOME/mingw-root/usr/bin:$PATH"
cd "$(dirname "$0")/../rebuild/hook"
i686-w64-mingw32-g++-win32 -shared -static -static-libgcc \
  -static-libstdc++ -O2 \
  dll_main.cpp hooks.cpp trampoline.cpp exports.def -o winmm.dll
file winmm.dll
echo "OK: drop winmm.dll next to gamemd.exe; copy the system"
echo "C:\\Windows\\SysWOW64\\winmm.dll alongside it as winmm_real.dll."
