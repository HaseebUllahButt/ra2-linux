# Static recompilation feasibility: result

Recommendation: YES. Use the mechanical lift as the base for the full game.

## Method

- `tools/lift.py` (Python, Capstone) turns each x86 instruction into C with the same register, flag and memory work.
- Registers and flags are C locals. x87 uses `long double` on the real FPU with the game's control word.
- Direct and virtual calls go through one dispatch table. Unknown targets run the original code through a bridge.
- Jump tables become C switches. Imports go through the 1:1 import table and stand-ins.
- Every test runs the whole lifted game (12,405 functions). Calls, also virtual calls, stay inside lifted code.
- Test runner: the 1:1 difftest (same inputs, return values, memory hashes, strict mode).
  Lifted code enters from the same call site, so it uses the same stack addresses as the original.

## Results

| Set | Tested | Exact | Wrong | Same fault as original (cannot prove) | No test |
|---|---|---|---|---|---|
| 1:1 proven | 31 | 30 | 0 | 1 | 0 |
| 1:1 failing | 35 | 22 | 0 | 12 | 1 (float arguments) |
| Special (x87, jump tables, virtual calls, strchr) | 11 | 11 | 0 | 0 | 0 |

- "Same fault": the original code faults or stops on that test input. The lifted code faults at the same address with the same memory. These need better inputs, not lifter work.
- Special tests: 5000 inputs each for x87 functions, all values for tables. Bit-exact x87 results under 64-bit and 53-bit precision.
- Lifter bugs found by the tests and fixed: rep prefixes, entry not at the lowest address, bridge return slot, x87 exception bits.

## Whole game

- 12,423 functions. 12,405 lift (99.86 %). All 12,405 compile with gcc -m32.
- Cannot lift: 8 MMX, 2 cpuid, 2 rdtsc, 5 jump tables without a clear size, 1 flag read across a call.
- 272 jump tables, 21,951 indirect calls, 66 indirect tail jumps, 16,410 x87 instructions in 875 functions, 15 functions use fs: (exception frames).
- Speed: lift 5,600 functions per minute (one core). Compile 1,380 functions per minute (two jobs). Full game from zero: about 12 minutes.

## Imports (reports/imports.json)

| Group | DLLs and count | Our adapter on rebuild SDL (a) | Wine (b) |
|---|---|---|---|
| Easy | KERNEL32 160 (files, heap, time, threads, TLS, locale), WINMM 5, VERSION 3, ADVAPI32 9 (registry to INI), SHELL32 1, IMM32 5, COMCTL32 7, serial port calls | 6-8 days | covered |
| Medium | USER32 96 (window, messages, input, dialogs), GDI32 22 (text), WSOCK32 19 (sockets) | 12-18 days | covered |
| Hard | DDRAW 1 + COM surfaces, DSOUND 1 + COM buffers, BINKW32 13, OLE32 15 + OLEAUT32 11 (save games in structured storage, CoCreateInstance for Blowfish and online) | 18-30 days | covered; Bink uses the game's own DLL |

- (a) Reuse the rebuild: SDL window and input (app/platform.cpp), audio, FFmpeg Bink player (video/bink_player), MIX files and Blowfish (fs/).
- (b) Wine 11 here is the new 32-on-64 type. It has no 32-bit Linux part, so Wine cannot be a library in our 32-bit Linux process.
  The Wine way means: build the lifted C as a Windows program (needs a mingw or clang Windows toolchain) and run it under Wine. About 5-10 days.
  It does not give a native Linux game and does not use the SDL rebuild. Use it only as a test bed while (a) grows.

## Combined pipeline

1. Base: the mechanical lift of every function. It is exact by construction. The difftest proves it.
2. Readable layer: a 1:1 function that passes the difftest replaces the lifted one in the dispatch table.
   A generated adapter moves arguments from the lifted registers and stack to the C call (from the difftest signature rows).
   Its callees go back through `rc_call`, so no original x86 code runs.
3. Platform: the rebuild's SDL code implements the import stand-ins (option a).
4. Reuse from ra2-1to1: image mapping at 0x400000 (pilot_rt.c), thread block and import table fill (hybrid.c), stand-ins, difftest, fixtures, signature rows, abi_overrides.json.

## Estimate (one engineer; parallel agents shorten it)

| Work | Days |
|---|---|
| Lifter: last 18 functions, entry points for callbacks (window procedure, thread starts, COM tables), threads | 4-7 |
| C++ exceptions and SEH dispatch (RaiseException, RtlUnwind) | 3-5 |
| Windows layer (a) | 36-56 |
| Integration to a playable skirmish | 10-20 |
| Total | 53-88 |

## Main risks

- Windows layer size, mainly DirectDraw, DirectSound and the OLE save files.
- Code reached only through pointers that Ghidra did not mark as functions. The runtime stops with a clear message; add the entry and lift again.
- Exceptions across lifted frames. Threads (state R must become per thread).
- Speed of lifted code is not measured. The game is from 2000, so a 2-3 times slowdown is acceptable.
- Test inputs: 13 of 66 tested functions fault in the original on their inputs. Better fixtures are needed to prove them.
