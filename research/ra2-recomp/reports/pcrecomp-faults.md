# pcrecomp lifter faults checked against our lifter (J5b)

Outcome: 36 fault classes from pcrecomp's `CHANGELOG.md` and `difftest.py` were checked against our lifter.
14 classes were not covered, only partly covered, or masked by our fuzzer. Each now has tests that run real bytes against lifted C:
116 extra single forms and 69 multi-instruction sequences (31 of them real game code).
They found 6 bugs (7 wrong rules). 2 of the bugs are in code game.exe uses. All are fixed.
The whole fuzzer is back to 0 mismatches.

Source: `ref-community/sp00nznet_pcrecomp` (MIT), `CHANGELOG.md` and `tools/lift/difftest.py`
(its two "known" differences, `sbb.idiom` and `span.carry.default`, are both the stale-carry class, row 1).
We took only the list of traps. No code was copied. Their x87 (C `double`) is not used.

## Bugs found and fixed

In game code:

1. **CPU check lost the AC and ID flags** (`tools/lift.py`). `popfd` dropped bits 18 (AC) and 21 (ID),
   and `pushfd` always pushed them as 0. Two CPU checks in game.exe (`0077f14d`, `0077f282`) flip these bits and
   read them back. The lifted code saw no change and decided the CPU is a 386 with no CPUID and no MMX.
   The real code finds CPUID and MMX. Fix: `popfd` keeps AC and ID in `R.eflags`, `pushfd` pushes them back,
   a FLAGRET `ret` keeps them. They never reach the real CPU (`rc_entry_c` masks the flags with `0x8d5`).
   Found by `seq: game CPU check ...` (500 of 500 states wrong before the fix).
   **Note for the lead:** the lifted CPU check now answers like the real one. Code after it may now take
   its MMX path (`[0x8015a2] = 1`). `tools/run_groups.sh` was not run here (it runs the game).
2. **`fnsave` and `frstor`** (`rt/rc.h`; game: one each, in the CRT math-error path at `007822e2`/`007822ee`).
   These forms were skipped by J5; the J16 lifter now accepts them, so the fuzzer now runs them.
   `fnsave` wrote 0 in the unused upper halves of the first words; the CPU writes `0xffff`.
   `frstor` cleared the exception bits instead of loading them from the image, so a later `fnstsw` saw 0.
   Fix: write `0xffff` there; `frstor` ends with `fldenv` of the image's control word and exception bits
   (the real FPU stack stays empty; TOP and C0-C3 stay in `R`).

Not in game code (the lifter accepts these forms, so they were wrong for any later use):

3. **`fucom`, `fucomp`, `fucompp` raised "invalid" for a quiet NaN.** They used `fcmp`, whose `a > b` becomes
   `fcomip`. New `fucmp()` in `rt/rc.h` uses quiet compares (`fucomip`). Found by `seq: fucompp; fnstsw; sahf ...`.
4. **`bt`/`bts`/`btr`/`btc` on a 16-bit memory operand with a register offset** read the offset as unsigned.
   The CPU reads it as signed 16-bit (pcrecomp #89). Fix: sign-extend at the operand size.
   Found by the new extra forms `660fa306` etc. (124-250 of 500 states wrong before the fix).
5. **`fsin`, `fcos`, `fsincos`, `fptan` out of range** (finite, |x| >= 2^63): the CPU sets C2, leaves ST(0)
   and pushes nothing. The lifted code always cleared C2 and changed or pushed. Also, for a NaN or infinity,
   `fptan` and `fsincos` push a NaN, not 1.0 or a separately computed cosine. Fix: `x_trig_out()` test, and
   `fptan`/`fsincos` run as one real instruction that returns both values (pcrecomp's "clear C2" fix was incomplete
   in the same way).
6. **`fprem`, `fprem1` condition bits after a NaN, an infinite dividend or a zero divisor**: the CPU clears C1, C2
   and keeps C0, C3 (checked on this machine). The lifted code copied the real FPU's stale bits.

Fuzzer fix: `tests/fuzz/harness.c` leaked the two 64 KB arena copies on every run (about 400 MB over a full run).
The heap then grew over the made-up addresses of the new tests and they could not be mapped. Fixed (free all).

## Table

"Game uses it" comes from `tests/fuzz/forms.json` (all 990,485 instructions the lifter finds in game.exe).
"Before" is what the J5 fuzzer already checked. "Now" is what this job added. All rows pass now.

| # | Fault class (pcrecomp entry) | Game uses it | Fuzzer before (masks) | Now | Result |
|---|---|---|---|---|---|
| 1 | Stale carry in `adc`/`sbb`: `cmp`/`add`/`sub` did not publish CF (sbb idiom, `precise_carry`, difftest "known" x2) | yes: `sbb` 750, `adc` 22 (`neg; sbb` 439) | each form alone; our flags are plain C variables written by every instruction | 15 carry sequences (11 real: `neg; sbb`, `sbb; sbb -1`, `add; adc`, `shr; rep stosd; adc`, 64-bit `shr`/`rcr` loop, `shld` chain) | pass |
| 2 | `inc`/`dec` must keep CF | yes | yes, CF compared | `stc; inc; adc`, `cmp; dec; setb` | pass |
| 3 | Signed/unsigned `jcc`/`setcc` after `or`/`xor`/`test` (lazy flag kind) | yes | yes, all `jcc`/`setcc` forms with random flags | `xor/or/test; setge/setle/setg`; all 16 short `jcc` and all 16 `setcc` as extras | pass |
| 4 | `jp`/`jnp` and PF of narrow results | PF via `test ah`; no `jp`/`setp` in game | PF compared everywhere | `jp`, `jnp`, `setp`, `setnp`, `jo`, `jno` as extras | pass |
| 5 | Static flag state crossing a branch target / join point | yes | single instructions only | real saturating add with 2 join points (`0077ee6b`); `fnstsw` shapes with internal jumps | pass |
| 6 | Flags across `call`/`ret`/tail jump | FLAGRET `0x787665` only | no | not run (see "Not covered"); the lifter refuses all other cases | by design |
| 7 | `sahf` loads SF ZF PF CF | yes, 39 | yes | 13 `fnstsw; sahf; jcc` sequences | pass |
| 8 | `pushfd`/`popfd` round trip (`recomp_eflags`: CF, DF) | yes, 26 / 36 | yes; system bits masked (AC and ID never compared) | `stc; pushfd; clc; popfd; adc`, `std; pushfd; cld; popfd`, real saturating add, 2 real CPU checks; single `pushfd` now compares AC and ID | **bug 1, fixed** |
| 9 | AF of 8/16-bit results | no reader (`lahf`/`daa`/`aaa` not used) | masked (not modelled) | `lahf` extra (AF masked); reader search over game.exe | no reader |
| 10 | x87 compares: NaN gives C3=C2=C0; `fucompp` missing | `fcom` 128, `fcomp` 886, `fcompp` 99; no `fucom*` | `fcom*` yes; `fucom*` not run | `fucom`, `fucomp`, `fucompp` extras; `fcom`/`fucompp`/`ftst` + `fnstsw` + `sahf`/`test` sequences | **bug 3, fixed** |
| 11 | MSVC float compare `fnstsw ax; test ah; jcc` / `sahf; jcc` | yes, 1,115 `fnstsw ax` | each part alone | 15 of the 22 shapes as real code (the other 7 only add a `mov`/`inc` in between), plus 5 made up | pass (known DE bit in AL masked) |
| 12 | Leaked/lost x87 slots (`fucompp` pops, `fst st(i)` did nothing) | yes | TOP and tags compared after every x87 form (J5 fixed `fst/fstp st(i)`) | 5 stack sequences (`fld; fld st0; fmulp; fstp`, `fld1; fldz; fucompp`, `fst st2; fstp st1`, m64/m80 round trips) | pass |
| 13 | `fsincos` order; `fsin`/`fcos`/`fptan` C2; `fprem` C2; `fxam` | no | not run | all as extras | **bugs 5, 6, fixed** |
| 14 | `frndint` and `fist` round by the control word | yes | yes, random rounding mode | `fist m16/m32` extras | pass |
| 15 | `fld`/`fstp` m80, `fild`/`fistp` m64 exact, `-0` keeps its sign | `fld m10` 6, `fild m8` 229, `fistp m8` 2 | yes; values include -0 | m80/m64 round-trip sequences, `fstp m80`, `fistp m64` extras | pass |
| 16 | `rep cmps`/`scas` with ECX = 0 keeps flags | `repe cmps` 231, `repne scasb` 1,086 | yes, ECX = 0 in 1 of 4 states | `cmp; repe cmpsb; sbb`, `cmp; repne scasb; sete/setb` | pass |
| 17 | `rep movs`/`stos` with DF = 1; overlapping copies | `std` 1 (real `std; rep movsd`) | DF random; source and destination never overlapped | real `std; rep movsd; cld` and `std; repne scasb`; 4 overlap sequences (`K_OVL`) | pass |
| 18 | 8/16-bit one-operand `mul`/`imul`/`div`/`idiv` use AX, DX:AX; CF/OF; `INT_MIN / -1` | 32-bit forms and `imul r8` | yes; a divide fault must happen on both sides | 8 narrow forms as extras; `cdq; idiv`, `xor edx; div` | pass |
| 19 | `sar` on byte/word is arithmetic | yes | yes | `sar al, cl`, `sar ax, cl` extras; `cmp; sar al, cl; setl` | pass |
| 20 | `rcl`/`rcr` 32-bit lost the carry | `rcr r32, 1` 6 | yes | `rcl`/`rcr` by `cl` at 8/16/32 bits; real 64-bit `shr`/`rcr` loop | pass |
| 21 | `shld`/`shrd`: OF only for count 1; counts | `shld` 10 | yes (OF masked for count > 1) | `shrd`, `shld cl`, `shld 0`; `cmp; shld/shrd cl; setb/sete` | pass |
| 22 | Shift by 0 keeps flags; count masked to 5 bits; count >= width | yes | yes, `cl` counts include 0 and 32 | `cmp; shl cl; setb/sete`, `cmp; shr dx, cl; sete` | pass |
| 23 | `bt*` on memory: bit string, signed offset | `bt`/`bts [esp], eax` only | yes for 32-bit | 32 and 16-bit memory forms with a register offset | **bug 4, fixed** |
| 24 | `bt; jae` inverted | `bt` then `jcc` | yes | `bt; setae`; register forms | pass |
| 25 | `bsf`/`bsr` | no | not run | extras (undefined parts masked) | pass |
| 26 | `loop`/`loope`/`loopne` test ZF; `jecxz` | `jecxz` 1 | `jecxz` yes | `loop`, `jcxz` extras; the lifter refuses `loope`/`loopne` | pass |
| 27 | `lock` forms, `xadd`, `cmpxchg`, `cmpxchg8b` | `lock inc/dec` 6 | `lock inc/dec` yes | `lock xadd`, `xadd`, `lock cmpxchg`, `cmpxchg` extras; the lifter refuses `cmpxchg8b` | pass |
| 28 | 8/16-bit partial registers | yes | yes (r8h, r16 forms) | `ah/al` mix with `xchg`, `movsx`, `sar cx`; `add ax; adc dx; sbb si` | pass |
| 29 | `imul` 3-operand overflow (CF/OF) | yes | yes | `imul r,r,imm32; seto; setc`, `imul r16,r16,imm8; seto` | pass |
| 30 | x87 state save/restore (`fnsave`/`frstor`; pcrecomp lists them with the 80-bit fixes) | 1 / 1 | skipped (lifter refused them then) | run now as game forms | **bug 2, fixed** |
| 31 | `xlatb` missing | no | - | the lifter refuses it | safe |
| 32 | `push`/`pop` of segment registers, 16-bit `push`/`pop`; far `call`/`retf` | no | - | the lifter refuses them | safe |
| 33 | `repz ret` | no | - | lifted as `ret` | safe |
| 34 | MMX ops silently left out | yes, 128 | skipped (checked by `tests/mmx`) | - | covered elsewhere |
| 35 | `cpuid`/`rdtsc` | yes, 7 | skipped (J16 hooks) | - | covered elsewhere |
| 36 | Function discovery faults (jump table into itself, false entries, `loop` as block leader, gaps) | - | - | not an instruction rule | out of scope |

## What was added

- `tools/insnfuzz.py`:
  - `EXTRA`: 116 single forms that game.exe does not use but the lifter accepts (8/16-bit `mul`/`div`,
    `rcl`/`rcr` by `cl`, `shrd`, `bsf`/`bsr`, `bt*` on memory, `xadd`/`cmpxchg`, every short `jcc` and `setcc`, `loop`,
    `jcxz`, string ops without `rep`, `enter`, and the x87 forms pcrecomp got wrong: `fsincos`, `fsin`, `fcos`, `fptan`,
    `fprem`, `fprem1`, `fxam`, `fucom*`, `fst st(i)`, 80-bit and 64-bit loads and stores, `fist`).
    They run at made-up addresses (`0x40000000` up) with the same masks as the game forms. Keys start with `extra:`.
  - `SEQS`: 69 multi-instruction sequences, 31 of them real code from game.exe. Keys start with `seq:`.
    Each is lifted with the real `Emitter`, one instruction after another, internal jumps as `goto`s.
    The real bytes run natively up to the end of the sequence (one outside jump target is allowed).
    Real ones were picked by a search over every function: one example per shape of
    "x87 compare ... `fnstsw ax` ... `jcc`" (22 shapes) and per "instruction before `adc`/`sbb`" pair (18 shapes),
    plus the `pushfd`/`popfd`, `std`, `rcr` and `shld` code found in the game.
- `tests/fuzz/harness.c`, `fz.h`:
  - `K_SEQ`: `ebp` always points into test memory, the memory operand is aligned, AF starts at 0,
    `R.eflags` starts as the input flags.
  - `K_OVL`: `edi` within 8 bytes of `esi`, so `rep movs` copies overlap.
  - A second absolute data page (`xabs`), and up to 4 masked memory dwords (stale `pushfd` slots: AF only).
  - In x87 sequences, when the CPU raises "invalid", bit 1 of `eax` is not compared. This is the known J5
    difference (the lifted code also sets "denormal" when it converts a denormal memory operand), seen through
    `fnstsw ax`. The game reads only AH after `fnstsw ax` (J5 report).

Run only the new checks: `tools/insnfuzz.sh -f "seq: "` or `-f "extra: "`.

## Not covered (and why)

- Flags across calls and returns (pcrecomp's `RECOMP_FLAGS_OUT/IN`): our lifter refuses any function that reads
  flags set by a caller or a callee (`flag_check`), except the one FLAGRET helper `0x787665`. The FLAGRET hand-off
  (pack at `ret`, unpack after the call) is not run by a test; the bit positions were checked by reading.
- x87 values across native calls: the lifted x87 stack is the global `R`; moving it to and from the real FPU
  is in `rt/rc_rt.c` (not in this job's scope).
- AF: not modelled. game.exe never reads it: no `lahf`, `daa`, `das`, `aaa`, `aas`; every `pushfd` value goes back
  through `popfd` or is XORed and tested only at bits 18 and 21.
- Function discovery faults in pcrecomp are not instruction rules; they belong to discovery, not to this job.
- Forms the lifter refuses (the function is then not lifted, so they cannot be wrong): `loope`/`loopne`,
  `cmpxchg8b`, `xlatb`, 16-bit `push`/`pop`, segment register `push`/`pop`, `retf`, 16-bit `shld`/`shrd`, BCD,
  `fcmov`/`fcomi`.

## Done-check output

`tools/insnfuzz.sh` (2,000 states per run, x87 at 53 and 64-bit precision), after all fixes:

```
extra forms (not in game.exe) 116 of 116, sequences 69 of 69
1016 forms, 1172 forms tested (2566 examples), 29 forms skipped
example runs with mismatches: 0
SUMMARY examples 2566, runs 3025, compared states 6042875, examples with mismatches 0, mismatching states 0, setup problems 0
```

29 skipped (J5 had 32): `cmovne`, `fnsave` and `frstor` now run, because the J16 lifter accepts them.

Each fix was seen to fail first: AC/ID sequences 500 of 500 states, `fucompp` sequence 106 of 500,
16-bit `bt*` 124-250 of 500, `fnsave` and `frstor` every run, `fsin`/`fsincos`/`fptan`/`fprem*` some states
(500-state runs).
