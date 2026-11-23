# Hook DLL (rebuild phase 3)

Goal: run rebuilt engine code next to the original gamemd.exe on
Windows, one subsystem at a time, and verify each swap in a real game.

Approach: proxy DLL. The game loads WINMM.DLL at startup. Our
winmm.dll sits in the game folder; its listed exports forward to
winmm_real.dll. At PROCESS_ATTACH we install a 5-byte jump for the
first enabled replacement, MixFileClass::Bootstrap (0x005301A0).

Status: Bootstrap bridge implemented, Linux simulation tested, and
PE32 DLL cross-compiled. Live Windows execution remains pending.
Read [HOOKING.md](HOOKING.md) for the global/ABI contract, native
constructor compatibility boundary, verification limits and Windows
drop-in test procedure. Bootstrap fills Array and six Generics slots;
Movies is preserved for its later initialization stage. This full
replacement does not call the overwritten original or use a trampoline.

Files:
- trampoline.{h,cpp} - x86 jmp-patch writer + trampoline builder
  (portable logic, unit-tested here with a simulated code page).
- hooks.cpp - the hook table: original address -> rebuild function.
  Bootstrap enabled; other landmarks remain disabled.
- bootstrap_hook.{h,cpp} - rebuilt Bootstrap entry, fixed-width game
  layouts, native constructor/vector adapters and debug logging.
- dll_main.cpp - DllMain + install sequence (Windows only).
- exports.def - winmm export forwards to the real DLL.
- test_trampoline.cpp - Linux unit test for the patcher.
- test_bridge.cpp - Linux global/ownership and call-boundary simulation.

Build from the repository root:

```sh
make -C rebuild/hook test
make -C rebuild/hook windows \
  WIN_CXX=/path/to/user/mingw-root/usr/bin/i686-w64-mingw32-g++-posix
```

Use `WIN_CXX=i686-w64-mingw32-g++` with a normal MinGW installation.
The new output is `build/winmm.dll`; the older `winmm.dll` beside this
README remains the skeleton artifact. All build products and fixtures
stay under `build/`. The existing root-level build script is not used
by this new build.
