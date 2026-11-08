# First replacement: MixFileClass::Bootstrap

`hooks.cpp` now installs an actual five-byte `E9 rel32` jump at `0x005301A0`
to `bootstrap_hook`. All other table entries remain disabled. The old
`rebuild/hook/winmm.dll` is the earlier skeleton; the new build artifact is
**`rebuild/hook/build/winmm.dll`**.

The replacement calls `ra2::fs::MixFileSystem::bootstrap(YurisRevenge)`.
That rebuilt implementation discovers `EXPANDMD99.MIX` down to
`EXPANDMD00.MIX`, opens and validates present archives, then mounts the six
required archives in the order below. It also resolves nested archives.
Only after this preflight succeeds does the bridge publish native objects.

This first hook replaces Bootstrap's orchestration. **Native MIX construction
and reading still run in the game.** Each rebuilt mount is materialized by the
original constructor at `0x005B3C20`, using the game's allocator at
`0x007C8E17`, a `0x28`-byte allocation, and the game's key at `0x00886980`.
The constructor allocates its own filename and header data, installs the native
vtable, and registers the object in `MixFileClass::MIXes`. The bridge checks
native entry count and data size against the rebuilt parser before publishing
the pointer. This deliberately parses archives twice. It provides native
objects for the existing game readers, vector operations and destructors;
casting rebuilt `shared_ptr` objects into the game would not satisfy that ABI.
The rebuilt filesystem is temporary; native objects retain ownership after it
is destroyed. This hook does not replace the constructor or filesystem lookup.

## Global contract

The direct writes below follow `rebuild/decomp/0053/FUN_005301a0.c`.
Addresses are preferred VAs for the supported fixed-base gamemd.exe 1.001.

| Storage | Bootstrap result |
| --- | --- |
| `0x00884D90` Array | Append a native pointer for each present EXPANDMD archive, highest number first. Preserve existing elements and the vector vtable. |
| `0x00884DF8` Generics.RA2MD, index 0 | Native `RA2MD.MIX` object. |
| `0x00884DFC` Generics.RA2, index 1 | Native `RA2.MIX` object. |
| `0x00884E48` Generics.CACHEMD, index 20 | Native `CACHEMD.MIX` object, possibly nested. |
| `0x00884E4C` Generics.CACHE, index 21 | Native `CACHE.MIX` object, possibly nested. |
| `0x00884E50` Generics.LOCALMD, index 22 | Native `LOCALMD.MIX` object, possibly nested. |
| `0x00884E54` Generics.LOCAL, index 23 | Native `LOCAL.MIX` object, possibly nested. |
| `0x00ABEFD8` MIXes | Native constructor links every new object into the existing intrusive list in mount order; preserve earlier mounts. Game lookup traverses this list. |
| `0x00884DE0` Movies | **No write.** A fresh process still has an empty movie vector after Bootstrap. The resource stage `0x00530460` fills movie ownership later. |
| `0x00884DA8` Maps, `0x00884DC0` Array_Alt, `0x00884DD8/DC` MULTIMD/MULTI | No write; these belong to other initialization stages. |
| Other 26 Generics slots | Preserve, including LANGUAGE/LANGMD, movie, theater, cameo, and map ownership. |
| `0x00886980` key object | Input to native construction; preserve. |

`GameGlobals32` models the contiguous storage from `0x00884D90` through
`0x00884E77`. Each game pointer is a `uint32_t` even on Linux ARM64.
The dynamic vector is `0x18` bytes: vtable +0, Items +4, Capacity +8,
IsInitialized +12, IsAllocated +13, padding +14, Count +16,
CapacityIncrement +20. Growth calls the existing native virtual
SetCapacity at vtable+8, so the game's heap and allocation flag remain valid.
`GameMix32` captures the native `0x28`-byte object layout for metadata checks.
Compile-time assertions guard the wire layouts.

Bootstrap calls `0x004790B0` once on entry and once on success, as the
decompile does. The rebuilt layer treats the cache-check methods as the
unconditional-success stubs documented in `rebuild/src/fs/README.md`.
Original progress logging is replaced by the debug lines described below.
Overlays (`0x00530000`), resources, language setup and movies remain separate
game stages.

## Calling convention and installation

YRpp declares Bootstrap as a **static, zero-argument** function, despite its
`JMP_THIS` wrapper. There is no valid `this` to consume. The replacement uses
two unused fastcall register parameters to absorb arbitrary ECX/EDX values;
there are **zero stack parameters**. It returns the decompiled 0/1 result in
EAX with a plain `ret`, preserving the x86 nonvolatile registers through the
compiler-generated prologue. Its MinGW symbol is `@bootstrap_hook@8`; the
decoration includes the two register arguments and does not imply `ret 8`.

Native constructor/resize/destructor calls use fastcall function pointers
with an extra dummy EDX parameter. ECX carries the native object; filename
and key (or resize capacity and buffer) occupy the two stack arguments the
native thiscall callee pops. Allocation uses cdecl. No C++ exception may
unwind from the replacement into a game frame.

Installation runs synchronously in `DllMain`, before the EXE entry point.
It only checks the main image, changes page protection, writes the jump,
flushes the instruction cache, restores page protection, and emits a debugger
line. No archive work occurs under the loader lock. Deferring installation
to a worker thread could miss Bootstrap.

The installer accepts only an i386 PE32 image at `0x00400000`, checks that
the site is within its image, and rejects common existing detours/breakpoints.
These checks **do not establish executable identity**: no original-byte
signature or executable hash was available within this task's read scope.
Use the matching unmodified Yuri's Revenge 1.001 executable. Other patched
executables remain unverified.

This full replacement never calls the overwritten code, so no instructions
are relocated and no original-function trampoline is used. The existing
trampoline implementation and test are unchanged. Null entries are skipped
and only an actual patch increments the installation count.

## Linux verification and Windows build

From `/tmp/r2`:

```sh
make -C rebuild/hook test
make -C rebuild/hook windows WIN_CXX=i686-w64-mingw32-g++
```

On this REFERENCE_SYSTEM, the second command uses the existing relocated compiler:

```sh
make -C rebuild/hook windows \
  WIN_CXX=/path/to/user/mingw-root/usr/bin/i686-w64-mingw32-g++-posix
```

All outputs, fixtures and compiler temporary files go under
`rebuild/hook/build/`. No source from `rebuild/src/fs/` is changed.
The DLL statically links compiler/runtime support and retains the 16
`winmm_real` forwarders from `exports.def`.

`test_bridge.cpp` binds simulated game memory to the **same replacement
entry**, passes arbitrary values for its two ignored register inputs, and
asserts its return value. Independent small MIX fixtures exercise three
expansions, four nested required archives, both top-level base archives,
native-list registration, Array growth, all six ownership slots, and
byte-for-byte preservation of unrelated globals. Fake native callbacks
model the game's heap/list boundary using 32-bit handles; they do not call
addresses in the executable. Additional cases cover malformed and missing
archives, native constructor failure, vector failure with deletion of the
unpublished object, and rejection of already-populated ownership slots.
Movies and overlays present on disk must not be mounted by Bootstrap.

Failure returns EAX=0 and logs a reason. Preflight errors publish no native
state, a stricter behavior than the original's partial mounts. A later native
failure retains already-published slots and list nodes for game cleanup;
there is no fallback call into the overwritten original. Re-entry with any
of the six ownership slots populated fails instead of leaking duplicate mounts.
Bridge callbacks must not throw.

Verified here:

* Strict C++17 builds with `-Wall -Wextra -Wpedantic -Werror`.
* Linux bridge tests and the existing trampoline tests pass.
* The Windows DLL cross-compiles as PE32 i386. Its only DLL imports are
  KERNEL32.dll and msvcrt.dll; all 16 listed exports forward to winmm_real.
* Disassembly confirms the replacement's plain `ret`, EAX result, and EBX
  save/restore; unused ESI/EDI/EBP are preserved. The native constructor call
  loads ECX with its allocation, passes filename/key on the stack, and
  accounts for the callee's eight-byte stack cleanup.
* The pre-existing filesystem report verifies all 16 installation archives
  (13 encrypted). This task's bridge tests use generated fixtures; they do
  not repeat that real-installation verification.

**Pending Windows testing:** DLL loading and forwarding in a real game,
the executed x86 call/return path, native object metadata agreement, allocator
and destructor compatibility, subsequent game archive reads and full startup.
An ARM64 logical ABI simulation plus cross-compilation is not an executed
x86 ABI test and does not prove a live-process replacement yet.

## Windows test procedure

1. Use a test copy of the matching gamemd.exe 1.001 installation with its
   normal MIX files. Set the working directory to that game directory.
2. Copy **`rebuild/hook/build/winmm.dll`** into the game directory as
   `winmm.dll`. Copy the **32-bit** Windows system winmm.dll alongside it as
   `winmm_real.dll`. On 64-bit Windows the 32-bit source is
   `%WINDIR%\SysWOW64\winmm.dll`; on 32-bit Windows it is
   `%WINDIR%\System32\winmm.dll`. The proxy exports are forwarders, so
   `winmm_real.dll` must be present before process startup.
3. Capture `OutputDebugStringA` with a debugger or DebugView before launching
   the game. Start a fresh process. Expect exactly one installation line:

   ```text
   [ra2hook] installed MixFileClass::Bootstrap @ 005301A0
   ```

4. When the game calls Bootstrap, both debugger output and the append-only
   `ra2-bootstrap-hook.log` in the working directory should contain:

   ```text
   [ra2hook] Bootstrap replacement entered
   [ra2hook] Bootstrap mount expandmd01.mix -> <native pointer>
   [ra2hook] Bootstrap mount RA2MD.MIX -> <native pointer>
   [ra2hook] Bootstrap mount RA2.MIX -> <native pointer>
   [ra2hook] Bootstrap mount CACHEMD.MIX -> <native pointer>
   [ra2hook] Bootstrap mount CACHE.MIX -> <native pointer>
   [ra2hook] Bootstrap mount LOCALMD.MIX -> <native pointer>
   [ra2hook] Bootstrap mount LOCAL.MIX -> <native pointer>
   [ra2hook] Bootstrap success: expansions=1 generics=6 movies=0 eax=1
   ```

   Expansion names, capitalization, count and pointers depend on installation.
   An installation with additional numbered EXPANDMD archives must show them
   in descending order. The installation line is debugger-only; file logging
   starts when Bootstrap runs outside DllMain.
5. Break on the replacement (or at `0x005301A0`) and inspect the `E9` target
   to confirm execution is in the new DLL. On return, verify EAX=1 and an
   unchanged caller stack pointer. Inspect Array.Count/Items, the six generic
   pointers, native object metadata and the linked `MIXes` list. Movies should
   still have its pre-Bootstrap contents. This establishes the live bridge
   contract beyond the logical Linux simulation.
6. Continue through the menu, start a skirmish to exercise subsequent native
   resource reads, and exit normally to exercise native cleanup. Record the
   executable version, logs, observed globals and result before marking the
   hook Windows-verified. Any `Bootstrap failure`, metadata mismatch, missing
   entry line, forwarding error, startup crash or shutdown crash leaves that
   verification pending. Removal of the two dropped-in DLLs restores the
   normal system winmm loading path.

## Sources

The supplied decompile establishes direct global writes and return paths.
`rebuild/src/fs/mix_bootstrap.{h,cpp}` and its README establish the rebuilt
mount contract and the prior archive verification. `analysis/community-crosscheck.md`
corroborates the address names. Only these YRpp raw sources were fetched:

* [MixFileClass.h](https://raw.githubusercontent.com/Ares-Developers/YRpp/master/MixFileClass.h): static Bootstrap, constructor/key, native fields and global ownership layout.
* [ArrayClasses.h](https://raw.githubusercontent.com/Ares-Developers/YRpp/master/ArrayClasses.h): vector layout, growth and allocation ownership.
* [GenericList.h](https://raw.githubusercontent.com/Ares-Developers/YRpp/master/GenericList.h): intrusive node and sentinel layout.
* [Memory.h](https://raw.githubusercontent.com/Ares-Developers/YRpp/master/Memory.h): native allocator address and heap ownership requirement.
* [YRPPCore.h](https://raw.githubusercontent.com/Ares-Developers/YRpp/master/YRPPCore.h): include/layout context during header discovery.

Failed candidate fetches were `Containers.h`, `VectorClass.h`, and
`DynamicVectorClass.h` under the same raw YRpp base (404). No other network
hosts or repositories were accessed.
