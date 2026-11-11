# Instruction fuzzer (J5): result

Outcome: 0 mismatches. All 984 runnable instruction forms of game.exe match the CPU on 2,000 random states each.
x87 forms pass at 53-bit and at 64-bit precision.

## Numbers

| Item | Count |
|---|---|
| Functions read (all-addresses.txt) | 12,418 (5 more stop at a jump table, see below) |
| Instructions found by the lifter's own search | 990,485 |
| Different instruction forms | 1,016 |
| Forms tested | 984 (2,376 real examples, up to 3 per form) |
| x87 forms (each run at 53-bit and 64-bit precision) | 183 forms, 409 examples |
| Forms skipped | 32 (list below) |
| States compared | about 5.56 million (2,000 per run; div/idiv: about 1,700, the rest fault on both sides the same way) |
| Mismatches | 0 |

## How it works

- `tools/insnfuzz.sh` runs it all. Options: `-n states`, `-p 53|64|both`, `-f text` (filter), `-s seed`, `-v`.
  Output is in `build/fuzz/run.log`; the last line is the summary. `INSNFUZZ_COLLECT=1` collects the forms again (3 minutes).
- `tools/insnfuzz.py collect`: walks every listed function with the lifter's own `Func.discover`.
  A form is: mnemonic + prefixes + kind and size of each operand (register class, memory shape, immediate size),
  plus marks for "same register twice" and "address register is also an operand". The list is `tests/fuzz/forms.json`.
- `tools/insnfuzz.py gen`: lifts each example with the real `Emitter` (no copy of its rules) and writes C.
  It also writes, per example, which results the Intel manual defines for that input (the mask).
- `tests/fuzz/harness.c` + `native.S`: maps a page at the instruction's real address and puts the real bytes there.
  Small stubs at the fall-through address, the branch target and one "indirect target" bring the CPU back.
  So branches, calls, `ret`, indirect jumps and indirect calls run too; the result says which way the code went.
- Each state: random registers (with many edge values), random flags and DF, random memory. Registers that form a memory
  address are chosen so the address lands in test memory; absolute addresses get a page mapped at that address.
  x87: random TOP, 0-8 values, condition bits and exception bits. Values include 0, -0, denormals, infinity, quiet and
  signalling NaN, the "indefinite" NaN, integers near the 16/32/64-bit limits, halves, and numbers that round at 53 bits.
  Rounding mode is random. A state where the real CPU reports an x87 stack overflow or underflow is not used.
- Compared: all 8 registers, CF PF ZF SF OF DF, all test memory (64 KB plus mapped pages), the branch result,
  x87 TOP, which slots hold values, all held values bit for bit, control word, exception bits, and the
  condition bits listed below. A fault (for example divide by zero) must happen on both sides with the same signal.

## What the lifter promises and what is masked

- Flags: the lifter computes every flag it writes, always (it has no dead-flag removal). So the fuzzer compares all five
  flags after every instruction, except the ones the Intel manual leaves undefined:
  AF (not modelled at all); after `mul`/`imul`: SF ZF PF; after `div`/`idiv`: all; after `bt*`: SF OF PF;
  after `bsf`/`bsr`: CF OF SF PF, and the target register when the source is 0;
  shifts and rotates: OF when the count is more than 1, CF for 8/16-bit `shl`/`shr` when the count is the size or more.
- `pushfd` and `lahf`: the AF bit and the system bits of EFLAGS are not compared.
- x87 condition bits: C0-C3 are compared for compares (`fcom*`, `fucom*`, `ficom*`, `ftst`, `fxam`, `fprem*`, `fninit`)
  and for every non-x87 instruction (must not change). After other x87 instructions Intel leaves C0, C2, C3 undefined.
  C1 (the "rounded up" bit after loads and math) is not modelled by the lifter. The game never reads it:
  every `fnstsw ax` in game.exe is followed by `test ah, 0x41/0x01/0x40`, `sahf`, `and eax, 0x3800` (TOP),
  `and eax, 0x100` (C0) or `and ax, 0x20` (precision bit).
- x87 exception bits are compared, with one exception: when the CPU raises "invalid" (a NaN operand), the lifted code may
  also raise "denormal", because it converts a denormal memory operand before the math. The game never reads the denormal bit.
- x87 values are always valid encodings (no "unnormals"); the game cannot make others.

## Bugs found and fixed

1. `fst st(i)` and `fstp st(i)` (x87 `DD D0+i`, `DD D8+i`) did not mark st(i) as holding a value.
   The real CPU does. Effect: a later `fld`, `fxch` or function exit saw st(i) as empty. Found in `fstp st(2)` at 0061c874.
   Fix in `tools/lift.py` (one line each). No change was needed in `rt/rc.h`.

Check that the fuzzer finds bugs: three wrong rules were planted on purpose (borrow in `sbb`, `fdivr` turned into `fdiv`,
`fprem` turned into `fprem1`). A 300-state run found the first two in 31 examples. `fprem` is not used by the game, so it is not a form.

## Skipped forms (32)

| Reason | Forms | Uses in game.exe |
|---|---|---|
| Lifter rejects MMX (`movq`, `movd`, `emms`, `pmulhw`, `punpck*`, `pack*`, `psr*`, `pxor`, `paddusb`, `pmullw`) | 21 | 128 (8 functions, already known) |
| Lifter rejects `cmovne` | 1 | 12 |
| Lifter rejects `fnsave`, `frstor` | 2 | 2 |
| `fs:` access (Windows thread block, exception frames) | 5 | 47 |
| `cpuid`, `rdtsc` (J16 owns these) | 2 | 7 |
| `int3` | 1 | 1 |

The full list with an example each is written to `build/fuzz/skipped.txt` on every run.
The lifter rejects the whole function for these forms, so they are already in the "cannot lift" list of `reports/feasibility.md`.
5 functions stop in discovery at a jump table without a clear size (004e64f0, 00578bc0, 00783290, 00788cb0, 0078a44e);
their instructions are not in the form list.

## Not covered

- Jump tables are tested only as a plain indirect jump; the C `switch` the lifter writes for a known table is not run.
- Single instructions only: the per-function flag check (a flag read must see a write in the same function) is not tested here.
- x87 forms run at 53 and 64-bit precision. 24-bit (Direct3D's default) is not run; the game sets 53 or 64.

## Done-check output

`tools/insnfuzz.sh` (2,000 states, x87 at 53 and 64-bit precision):

```
1016 forms, 984 forms tested (2376 examples), 32 forms skipped
skipped forms (cannot run alone or lifter rejects): 32
example runs with mismatches: 0
SUMMARY examples 2376, runs 2785, compared states 5564503, examples with mismatches 0, mismatching states 0, setup problems 0
```

`tools/run_groups.sh` after the fix (lifted 12,405 functions, 274 recompiled): all 98 lines equal `reports/groups.txt`
of the base commit. Every exact row is still exact; no new failures.

Note for the lead: `tools/run_groups.sh` line 7 empties `groups.txt.new.new` instead of `groups.txt.new` (typo), so a
leftover `.new` file from a stopped run would be added to the report.
