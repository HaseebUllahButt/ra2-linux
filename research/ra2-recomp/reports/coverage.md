# J2: full function list (coverage)

Result: `tests/entries-v2.txt` has **19,273 entries** (12,423 old + 6,850 new).
It covers **99.43 % of all non-padding bytes** of `.text` and **99.95 % of the code bytes**
(non-padding minus jump tables and other data). The old list covered 84.6 % of non-padding bytes.
Lift (after merging master with the J16 rules): **19,270 lift, 3 fail** (all 3 new, see below).

## Numbers

| | Old list | entries-v2 |
|---|---|---|
| Entries | 12,423 | 19,273 |
| Covered bytes | 3,101,638 | 3,603,024 |
| Covered / non-padding bytes | 84.60 % | **99.43 %** |
| Covered / code bytes | 91.13 % | **99.95 %** |
| Lift ok / fail | 12,423 / 0 | 19,270 / 3 |

`.text` is 3,769,805 bytes. Padding (int3, nop forms, zero runs between functions) is 145,958 bytes.
Padding counts only in gaps, so the old list shows less padding: its big gaps hide padding inside unlisted code.

## Gaps left (every gap has a class)

| Class | Bytes | Gaps | What it is |
|---|---|---|---|
| jump table | 14,529 | 466 (355 tables read by the lifter + 111 more) | Switch tables in `.text` (dword targets and byte index tables). Data. |
| other data | 4,438 | 78 | Byte tables (small values), bytes that do not decode, odd instructions. |
| dead code | 970 | 13 | Decodes, but nothing points to it and no clean start was found. |
| unlisted code | 32 | 1 | `0x606040` (see open issues). |

All gaps: `reports/coverage-gaps.tsv` (start, end, size, class).

## Where the new entries came from

Sources (one entry can have several; `tests/entries-v2-log.tsv` lists every candidate, its sources and verdict):

| Source | Added | Meaning |
|---|---|---|
| pcr | 4,626 | pcrecomp `new_starts.json` (ref-community `_out`), 5,163 given |
| ptr | 4,150 | 32-bit value in `.rdata`/`.data` or a code immediate pointing at the start |
| pad | 2,857 | first byte after padding (or after reached code) in an uncovered area |
| rtti | 310 | virtual methods from RTTI vtables; all 310 missing ones added (6,042 were already listed) |
| noret | 10 | byte after a call to a function that never returns, with no jump to it |
| seed | 2 | owner hand seeds `0x524FF0`, `0x7492D0` |
| call | 0 | every direct call target was already listed |

Exact source sets of the 6,850 new entries: pcr+ptr 3,403; pad only 2,168; pcr only 440; pad+pcr+ptr 391;
pad+pcr+ptr+rtti 197; pcr+ptr+rtti 112; pad+pcr 81; ptr only 25; pad+ptr 20; noret 10; seeds and rest 3.
So 4,626 of the new entries are confirmed by pcrecomp; 2,224 come only from our scan.
The 2,168 "pad only" are mostly real: 1,590 sit on a 16-byte boundary after int3/nop padding;
most others are CRT functions right after a `ret` and import thunks (`jmp [IAT]`).

### What happened to the pcrecomp list (5,163)

| Verdict | Count |
|---|---|
| added | 4,626 |
| rejected: a function runs into it by plain fall-through (it is a label) | 216 |
| rejected: splits an instruction of other new code | 110 |
| rejected: odd instruction (in/out/les/...) | 90 |
| rejected: reads a flag before setting it (start in the middle of code) | 66 |
| rejected: runs into a listed function without a call | 22 |
| rejected: looks like data | 13 |
| rejected: cannot decode / splits listed code | 3 |
| not tried: label inside reached code / inside a jump table | 10 / 10 |

Seed `0x6163A0` was left out: in RA2 `game.exe` it is the last byte of an instruction of `0x616350`
(it is a YR address). Seed `0x7492D0` replaced a wrong candidate (`0x749281`, a byte table).

## How it works

- `tools/coverage.py <list>`: runs `lift.Func.discover()` for every entry (2 processes, owner rule),
  joins instruction bytes, cuts padding, classes each gap. About 1 minute.
- `tools/find_entries.py <old> <new> [log] --seed TAG=FILE ...`: grows the list in rounds until no new start is found.
  A candidate is dropped when it does not decode, has odd instructions, starts like data, reads a flag
  before setting it, splits an instruction (of listed or new code), runs over a jump table or bytes that
  code reads as data, or runs into a listed function by plain fall-through. When one new function runs
  into another by plain fall-through, the second is a label and is dropped (unless it is a call target or
  an aligned start after padding). Then all new entries are checked together once more.
  Old functions keep their bytes: new entries are only in bytes no old function reaches, except the
  10 "noret" entries (and seeds at the same kind of place), which end an old function after a call that never returns.
  It takes 1 to 2 hours at 2 processes (the last rounds add 2 import thunks each).
- `tools/liftcheck.py <list> [--new-only OLD] [--fails FILE]`: lifts every entry, counts fails by reason.
- `tools/entrystats.py`: source counts above.
- Seed lists: `tests/seeds/` (pcrecomp new starts, RTTI methods, owner seeds).

Command used:

```
python3 tools/find_entries.py ra2-1to1/inputs/all-addresses.txt tests/entries-v2.txt tests/entries-v2-log.tsv \
  --seed seed=tests/seeds/owner-seeds.txt --seed rtti=tests/seeds/rtti-methods.txt --seed pcr=tests/seeds/pcrecomp-new-starts.txt
```

## Overlap check (entries-v2)

- Instructions that start inside another function's instruction: **0**.
- Shared code (two functions reach the same instruction starts through jumps): 195 new and 43 old functions.
  Same instruction boundaries, so it is safe; the code is lifted twice.
- Places where discovery stops because code runs into the next entry:
  53 new and 9 old functions stop after a call (the call never returns: the lifter emits a trap there);
  1 old function (`0x691310`) runs into `0x691320` by plain fall-through (was already so in the old list).

## Lift check

```
all   19273 entries:  19270 lift,    3 fail
           2  x87 d9 /6 (fldenv/fnstenv)
           1  instruction xlatb
old   12423 entries:  12423 lift,    0 fail
new    6850 entries:   6847 lift,    3 fail
```

Fails (all CRT x87 helpers, real code):

- `0x790df6`: `fnstenv [esp]` at `0x790fe3`
- `0x7910ae`: `fnstenv [esp]` at `0x79129b`
- `0x7873f0`: `xlatb` at `0x787444`

## Open issues for the lead (lifter rules; lift.py was not changed)

1. `fnstenv` (d9 /6) and `xlatb` are not lifted: 3 functions above.
2. Jump tables in old functions `0x783290` and `0x788cb0` (CRT memcpy/memmove) read past the table end.
   Targets like `0x5e08458b` come out, so discovery reports 16 "cannot decode" addresses outside `.text`
   for each. These are the 2 "decode problems" in the coverage output.
3. Plain fall-through into another entry becomes a trap. Example: `0x606040` (a real function, pointed to
   by data and pcrecomp) runs into old entry `0x606060`, which is its own final `ret`. If the lifter turned
   "falls into next function" into a tail call, about 110 such starts could be added safely.
4. 58 lifted functions have a trap after a call that our discovery treats as no-return because the next
   byte is another entry. If any of those calls does return, the game stops there with a clear message.
5. Pointer-based starts (ptr, pcr) are not proven. J3's census should confirm them.

## Done-check output

`python3 tools/coverage.py tests/entries-v2.txt`

```
entry list          tests/entries-v2.txt (19273 entries, 2 with decode problems)
.text bytes         3769805
padding bytes       145958
non-padding bytes   3623847
covered bytes       3603024
gap classes (bytes, gaps):
  jump table           14529    466
  other data            4438     78
  dead code              970     13
  unlisted code           32      1
covered / non-padding bytes         99.425%
covered / non-padding code bytes    99.949%  (code = non-padding minus jump tables and other data)
non-padding gaps (start end size class):
  0040dc06 0040dc12     12 jump table
  0040dc56 0040dc62     12 jump table
  00417551 00417560     15 jump table
  0041851d 00418528     11 other data
  0041853d 00418548     11 other data
  00418555 00418560     11 other data
  0041856d 00418578     11 other data
  00418585 00418590     11 other data
  00418599 004185a0      7 jump table
  00418cc9 00418ce2     25 jump table
  0041cea9 0041ced0     39 other data
  0041d0ad 0041d0d4     39 other data
  0041d249 0041d270     39 other data
  00431679 00431690     23 jump table
  00432ae5 00432bf5    272 dead code
  0043a1e9 0043a1fb     18 jump table
  0043f831 0043f835      4 other data
  00441e9d 00441eaa     13 jump table
  00442035 0044205a     37 jump table
  0046fbc1 0046fc74    179 jump table
  00478675 00478684     15 other data
  00482a71 00482a96     37 jump table
  00482c55 00482c7b     38 jump table
  004918f3 0049192e     59 other data
  004983e9 004985b9    464 dead code
  0049b966 0049b96f      9 jump table
  0049b97e 0049b988     10 other data
  0049b9d9 0049ba1c     67 other data
  0049eb2a 0049eb33      9 jump table
  004af5e1 004af645    100 jump table
  004b42f1 004b4308     23 jump table
  004b4316 004b432c     22 jump table
  004c1abd 004c1adb     30 jump table
  004c69ad 004c69ec     63 jump table
  004c6ed1 004c6f0f     62 jump table
  004c7df5 004c7e07     18 jump table
  004e630d 004e6333     38 jump table
  004e6349 004e636f     38 jump table
  004e6385 004e63ab     38 jump table
  004e63c1 004e63e7     38 jump table
  00606040 00606060     32 unlisted code
  ... 162 more (use -v)
```
