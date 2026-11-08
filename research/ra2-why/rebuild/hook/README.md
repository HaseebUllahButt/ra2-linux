# Hook DLL skeleton (rebuild phase 3)

Goal: run rebuilt engine code next to the original gamemd.exe on
Windows, one subsystem at a time, and verify each swap in a real game.

Approach: proxy DLL. The game loads WINMM.DLL at startup. Our
winmm.dll sits in the game folder; Windows loads ours first; every
export forwards to the real system winmm. At PROCESS_ATTACH we patch
the game's code in memory: each hook site gets a 5-byte jmp to our
rebuilt function; the replaced bytes live in a trampoline so the
original path stays callable.

Status: skeleton. The trampoline patcher and the proxy plumbing
compile and unit-test on Linux here; the Windows-specific bits
(DllMain, export forwarding) are written but only compile under
mingw-w64 (not installed on this REFERENCE_SYSTEM yet).

Files:
- trampoline.{h,cpp} - x86 jmp-patch writer + trampoline builder
  (portable logic, unit-tested here with a simulated code page).
- hooks.cpp - the hook table: original address -> rebuild function.
  Seeded with the verified landmarks; grows as layers land.
- dll_main.cpp - DllMain + install sequence (Windows only).
- exports.def - winmm export forwards to the real DLL.
- test_trampoline.cpp - Linux unit test for the patcher.
