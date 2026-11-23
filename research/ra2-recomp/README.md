# Static recompilation test for Red Alert 2 game.exe 1.006

This repo tests a mechanical x86 to C translation of game.exe.
Each x86 instruction becomes C that does the same register, flag and memory work.
Globals keep their game addresses. The game image is mapped at 0x400000.
The 1:1 track in ../ra2-1to1 supplies the runtime, the image mapping, the test inputs and the comparison test.
This repo does not change ra2-1to1.

## Files

- `tools/lift.py`: the lifter (Python and Capstone). It writes one C function per game function.
- `rt/rc.h`, `rt/rc_rt.c`, `rt/bridge.S`: register state, call dispatch, x87 helpers, bridge to original code.
- `rt/difftest_rc.c`: copy of ra2-1to1 difftest.c. Only the lifted entry changed: lifted code uses the same stack addresses as the original.
- `rt/special_main.c`: typed tests for x87 math, jump tables and virtual calls.
- `tools/mktest.py`: lifts, compiles (two jobs) and links one test binary.
- `tools/run_groups.sh`, `tests/groups.txt`: all comparison groups. Results go to `reports/groups.txt`.
- `tools/run_special.sh`: typed tests. Results go to `reports/special.txt`.
- `tools/rcbisect.py`: finds the lifted functions that cause a mismatch (RC_OFF turns functions back to original code).
- `tools/imports.py`: the import list in `reports/imports.json`.

## Commands

- Run `tools/run_groups.sh` for the full build and the comparison groups. The first run lifts and compiles all functions (about 12 minutes).
- Run `tools/run_special.sh` after that.
- Set `RC_TRACE=1` to print lifted calls. Set `RC_ALARM=<seconds>` to change the test time limit.

## Model

- Registers are C locals. They go to the global state `R` at calls and returns.
- Flags are C locals. The lifter rejects a function that reads flags set by another function.
- x87 values use `long double` on the real FPU. `fldcw` goes to the real FPU, so precision and rounding match.
- Transcendental and rounding instructions run as the same x87 instruction.
- Calls go through `rc_call`: lifted target or original code through `bridge.S`.
- Jump tables become a C switch over the table targets.
