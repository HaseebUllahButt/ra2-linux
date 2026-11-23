# Function checks against original x86 bytes

## Results

Counts below are functions. Each tested function receives 200 states at each precision.

| Set | Lifter | Exact | Differ | Same fault | Cannot test | Total |
|---|---|---:|---:|---:|---:|---:|
| x87 | Ours | 99 | 13 | 119 | 645 | 876 |
| x87 | Reference | 82 | 31 | 118 | 645 | 876 |
| Integer sample | Ours | 496 | 50 | 446 | 8 | 1,000 |
| Integer sample | Reference | 496 | 7 | 489 | 8 | 1,000 |

These counts include strict fault-address differences.
Both lifters match every integer-sample state where the original and the lifted body both return.
Our 50 integer differences all concern faults. Their seven integer differences also concern faults.
The integer function totals therefore do not rank normal arithmetic accuracy.

The scan excludes 643 x87 functions: 599 have unknown indirect calls or jumps in their closures.
Another 41 exceed eight closure members. Three require host or system state.
Five sampled integer functions have unknown recursive x87 depth. They are not executed.
The other cannot-test functions reach the call limit.
Matching-fault categories can also contain successful returns or timeouts.
No matching fault counts as a pass.

The harness completes 491,200 state comparisons per lifter.
That is 93,200 x87 comparisons and 398,000 integer comparisons per lifter.
All 2,724 emitted bodies compile and link: 1,362 per lifter, including closure members.
Closure members only run as original native bytes when called.
The final run also passes the periodic full-data tracking checks.

Counts below are individual states. Excluded functions have no state counts.

| Set | Lifter | Exact | Differ | Same fault | Call limit |
|---|---|---:|---:|---:|---:|
| x87 | Ours | 56,136 | 2,477 | 33,911 | 676 |
| x87 | Reference | 52,667 | 5,043 | 34,814 | 676 |
| Integer sample | Ours | 281,644 | 1,212 | 113,216 | 1,928 |
| Integer sample | Reference | 281,644 | 688 | 113,740 | 1,928 |

The x87 return differences affect four functions in our baseline and 29 in the reference.
Our baseline has 852 unequal returned states. The reference has 4,321.
Fault differences affect nine x87 functions in our baseline and three in the reference.
One reference function has both return and fault differences.

| Set | Lifter | Precision | Exact states | Differ | Same fault | Call limit |
|---|---|---:|---:|---:|---:|---:|
| x87 | Ours | 64 | 28,068 | 1,238 | 16,956 | 338 |
| x87 | Ours | 53 | 28,068 | 1,239 | 16,955 | 338 |
| x87 | Reference | 64 | 26,021 | 2,834 | 17,407 | 338 |
| x87 | Reference | 53 | 26,646 | 2,209 | 17,407 | 338 |
| Integer sample | Ours | 64 | 140,822 | 606 | 56,608 | 964 |
| Integer sample | Ours | 53 | 140,822 | 606 | 56,608 | 964 |
| Integer sample | Reference | 64 | 140,822 | 344 | 56,870 | 964 |
| Integer sample | Reference | 53 | 140,822 | 344 | 56,870 | 964 |

## Main causes and five examples

All examples below use 64-bit precision. `k` is the repeatable input index.
The x87 strings give the full ten-byte value, with the highest byte first.

| Function and input | Instruction | Original | Ours | Reference |
|---|---|---|---|---|
| `00496c70`, k=1 | `00496cca: fidiv dword [esp+4]` | ST0 `3ffee43b980826234869` | Same | `3ffee43b980826234800` |
| `0041b7f0`, k=7 | `0041b7f8: fnstsw ax` | EAX `00000002` | Same | `00000000` |
| `004070d0`, k=68 | `004070dc: div esi` | SIGFPE, integer divide fault | Same fault | Returns EAX `20008080` |
| `004ba5b0`, k=0 | `004ba5e8: fnstsw ax` | EAX `00007830` | `00007030` | `00004000` |
| `0040b870`, k=63 | `0040b88d: mov edi,[eax]`, then `0040b88f: mov ebx,[ecx+eax]` | SIGSEGV at `00000000` | SIGSEGV at `0000000e` | Same as original |

Double storage loses low x87 bits in the first example.
Our output matches the original for all 400 states of that function.
The reference differs on 108 states there, all under 64-bit precision.
Other reference differences include float-to-integer conversions and transcendental helpers.
The 64-bit precision setting exposes more reference differences: 2,834 versus 2,209 x87 states.

The reference builds `fnstsw` from comparison bits in the second example.
It omits the denormal-input exception bit, `0x02`.
This input contains positive subnormal doubles, which set that bit on the real FPU.
Other status examples lose TOP, the x87 stack position, and C1, a rounding condition bit.
EAX is observable even when the intended result is memory or ST0.

Their `div` emission guards the divisor with `if (_dv)` in the third example.
A zero divisor therefore skips division and returns.
The original raises an integer divide fault. Our emitted division raises the same fault.
These faulting inputs do not represent valid game objects or normal division inputs.
They still reveal a concrete fault-behavior difference.

The fourth example also exposes a baseline runtime limit.
Our native bridge clears the simulated tags, then pushes native results relative to the previous simulated TOP.
A native callee with x87 inputs can therefore shift the later simulated status word.
The `0x0800` difference here is one TOP position.
This is a bridge/status result, not evidence that our arithmetic lost precision.
We leave the runtime unchanged, as requested, and retain this difference in the totals.

The fifth example shows compiler load ordering on bad pointers.
Our emitted accesses are ordinary C loads. The reference accesses are volatile.
The compiled baseline reads the second address before the first at this pair.
The original reads the first address first.
Strict fault-address comparison counts that difference.
It does not imply a different result when both addresses are valid.
The object-state limits and these fault differences prevent a general accuracy ranking from this sample.

## Method

This report tests function bodies from the local source trees on 2026-11-12.
The game was not run.

The input is Red Alert 2 1.006 `game.exe`.
SHA-256: `73288c03b58d370be268ca6d156b4e33bfdb2066dc980359467d8852ff3b00df`.
The address list has 12,423 entries.
The scan finds 876 functions with x87 instructions.
The expanded call filter accepts 233 of these functions.
It accepts 4,009 functions without x87 instructions in their own bodies.
The integer sample selects 1,000 of those functions with Python seed `1482048850`.
This sample represents that filtered frame. It does not represent all game functions.

Each accepted function receives 200 states under precision words `0x037f` and `0x027f`.
Thus each function receives 400 state checks per lifter.
Both lifters receive the same inputs as the original bytes.
The build uses 32-bit GCC, `-O1`, `-mfpmath=387`, `-frounding-math`, and `-fwrapv`.
It uses at most eight workers.
Each worker maps the original image at `0x400000` with `pilot_map_image`.
It uses our unchanged `rc_run_native` and assembly bridge for native calls.
It does not start the game entry point or call `pilot_hybrid_init`.
The filter rejects import paths before execution.

Scratch memory starts at `0x20000000` and has 64 KiB.
Inputs cycle between zero words, finite floats, scratch pointers, and small integers.
Registers point inside scratch, with small integers also supplied in EAX and EDX.
Stack arguments mix scratch pointers and small integers.
Initial x87 values are exactly representable doubles.
The original and our lifter receive these values in 80-bit storage.
A control-flow scan finds the initial x87 depth across known branches and native calls.
It rejects unknown recursive depth and paths without a common valid depth.
Mapped pointers are valid memory. They do not guarantee valid objects or every function precondition.
Functions with stronger requirements can fault on both sides.

Before each call, the worker resets registers, scratch, guest stack, data, and FPU state.
It clears hardware status and condition bits after input generation, then loads the requested precision word.
It compares EAX, EDX, x87 depth, and every occupied ten-byte x87 value.
It also compares scratch hashes and data-change hashes.
The data range is `0x7c9000..0xb2d000`, as in `rt/special_main.c`.
Write protection records every changed data page.
The data hash includes changed pages and their offsets, against the common starting image.
Every 64th returned snapshot checks the tracking against a full data scan.
It does not compare flags, FPU status, other registers, or guest stack output.
Hash collisions remain possible.

Each function runs in its own process.
Signal handlers use an alternate stack, so a bad guest stack can still produce a result.
Matching faults require the same signal and signal code.
Memory faults must also have the same access address.
A matching fault is never an exact check.
The 20 ms CPU-time call limit classifies loops as cannot test.
An unavailable C body also means cannot test.
Function categories use this priority: differ, same fault, cannot test, exact.
Exact requires all 400 checks to return and agree.
A mixed function with one matching fault is listed under same fault.
Raw state counts show that distinction.

## Call handling and code generation

Both sides call original native callees through our bridge.
This isolates the emitted body under test.
The safe-call filter requires a known closure of at most eight functions.
The integer sample limits each closure member to 400 instructions.
The x87 set has no instruction-count limit and accepts known internal jump tables.
It rejects incomplete decoding, unknown indirect calls or jumps, thread-segment access, and system instructions.
The eight-function limit is a test limit. It is not a lifter limit.
Some entries need special caller stack layouts. The generic inputs do not cover those contracts.
Rejected x87 functions remain in the report as cannot test.

The driver uses our `Func.discover()` to supply the same instruction addresses and block targets to both lifters.
It calls their `generate.lift_function_linear()` with `generate.LinearInstruction` wrappers and their `Lifter`.
It sets `lifted=set()`, so direct calls use `RECOMP_ICALL`.
Their runtime resolver uses our `recomp_lookup_manual()` adapter to return a native-call wrapper.
Our function table is empty, so our calls also reach native bytes.
The adapter converts their double x87 stack at each native boundary.
It does not change their generated function bodies or header.
This tests their instruction emission. It does not test their discovery, splitting, or complete lifted call graph.
The driver leaves `precise_sbb` and `precise_carry` at their default values, both false.
It enables `RECOMP_LOCAL_REGS` and `RECOMP_FLAT_MEMORY`.
It does not enable the optional x87 ring or local cache.

## Reference code findings

Sources are under `../ref-community/sp00nznet_pcrecomp`.

- `tools/lift/lift32.py` emits instruction statements and tracks lazy flag expressions.
- `tools/lift/generate.py` builds function bodies, labels, local dispatch, and register saves at exits.
- `runtime/recomp32/recomp_types.h` defines memory, registers, flags, floating-point helpers, and calls.

`ADDR(va)` is identity with `RECOMP_FLAT_MEMORY`.
With `RECOMP_MEM_BASE`, it adds that fixed offset.
Otherwise, it adds `g_mem_base`. A zero base also gives identity addressing.

Registers live in globals such as `g_eax` and `g_esp`.
With `RECOMP_LOCAL_REGS`, each function loads `l_eax` and its peers.
Macros save these locals before calls and returns, then reload them after calls.
Lazy flags use a kind and saved operands.
The header exports returned flags through `g_flag_k`, `g_flag_a`, `g_flag_b`, and `g_flag_cf`.

Their x87 values use `double`, not 80-bit storage.
The default stack shifts eight doubles on push and pop. `g_fp_top` tracks depth.
Optional modes use a ring buffer and a local cache.
An integer shadow keeps some 64-bit integer loads and stores exact.
The simulated control word affects conversions. It does not turn double storage into 80-bit storage.
Some transcendental instructions use host math functions.

`RECOMP_CALL` pushes a dummy return address, saves registers and flags, then calls the C symbol.
`RECOMP_ICALL` resolves the address through manual overrides, lifted dispatch, and import bridges.
It saves and restores registers around the selected function.
A missing call target prints an error and sets EAX to zero.
It does not run original code by default.
A missing tail target prints an error and returns.
Our adapter supplies native fallback for these tests.

To emit one body, construct `Lifter(iat_map={}, lifted=set())`.
Supply decoded instruction wrappers and block leaders to `lift_function_linear`.
Include `recomp_types.h`, define `RECOMP_GENERATED_CODE`, and compile the returned C.
The checked-in driver gives the complete working procedure.

## Conformance and replay checks

Sources are under `../ref-community/sp00nznet_redalert2-recomp`.
We read these files. We did not execute their game tests.

`tools/conformance.py` checks eight startup milestones from host log lines.
They cover image mapping, entry, window creation, DirectDraw, primary surface, first blit, intro frames, and the menu.
The intro check needs at least five distinct recorded frame checksums.
It also reads lift statistics and counts unresolved tail-call labels against the dispatch table.
It compares milestone counts, lift errors, missing terminators, and unresolved tails with a saved JSON baseline.
A worse count fails the command.
Missing game files or the built host cause a successful skip.
Without an existing baseline, it writes one. `--update` also writes a baseline.
The default startup run lasts 300 seconds, with an outer timeout 60 seconds longer.
Linux normally runs the host through Wine.
These checks do not compare function outputs against original x86 execution.
Distinct intro checksums show changing frames. They are not a match against reference frames.

`docs/replays.md` explicitly marks replay support as design, not implemented.
It proposes recording frame-stamped `EventClass` orders, setup, seed, and frame CRC samples.
Playback would restore setup and seed, inject orders, and compare frame CRCs.
Observer controls would sit above playback.
The exact recording hook and RA2 address mapping remain open questions in that document.
The document supplies no implemented replay equivalence check.

## Reproduce and inspect

Run `python3 tools/xverify/driver.py prepare extend_x87 calibrate build execute` from the repository root.
Build output stays in `build/xverify/`, with an empty `.trackerignore`.

- `manifest.json`: selected addresses, input depths, filter results, and seed.
- `catalog.json`: scan details and emission errors.
- `compile.json` and per-body logs: compiler results.
- `run_ADDRESS.log`: fault details and first differences.
- `results.json`: all function and state counts.
- `python3 tools/xverify/summarize.py`: validate totals and print counts for each precision.

Generated C stays outside the commit because it derives from the game executable.
Only `tools/xverify/` and this report enter the commit.

Tool versions:

- GCC: `16.2.1 20260810`.
- Capstone: `5.0.7`.
- Reference toolkit commit: `91998d6d1be9482946a23b7f441062e27d88a152`.
- Reference game project commit: `4ecdea4dd12729696d5fe0a4296c49202695bc3e`.
