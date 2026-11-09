# Headless MP02T2 match

Run `rebuild/tools/test_headless_match.sh /path/to/user/ra2/game 10000`.
The script builds only the test and its source dependencies with C++20 and `-O1`.
It freezes `master` with `git archive`. Set `HEADLESS_REVISION` to test another compatible branch or commit.
It does not change that branch. All build files go in a temporary directory.

Each build runs two independent matches with seed 42 and two computer players.
Both use the installed MP02T2 map, base rules, production, economy, movement, combat, AI, and victory helpers.
The second build enables AddressSanitizer and UBSan, including stack traces.
A timeout bounds each executable to 600 seconds. Set `HEADLESS_TIMEOUT` to change this limit.
Redirect both output streams to a log to keep crash stacks and the last frame marker.
A failed build, crash, timeout, invalid Cell links, or replay difference returns a nonzero exit code.
A frame limit without a winner is a valid test result.

The test prints a hash every 100 frames and at the final frame.
The hash covers entity identity, type, ownership, life, position, health, targets, reload timers, mission, and live flags.
It also covers house cash and power, RNG state, ground Cell chains, occupancy, and overlay amounts.
It excludes unresolved fields, private helper queues, factory progress, projectile flight, and economy cargo.
Thus matching hashes test observable shared state; they do not prove complete engine determinism.
An unchanged checkpoint count signals possible inactivity, not a blocked CPU frame.
The progress stream and timeout detect frames that fail to return.

TODO-ORIGINAL: Replace the fixed nearby building placement and combat approach routes with original AI missions and base planning.
The existing helpers also retain their own TODO-ORIGINAL limits.
The test does not change map positions, grant income, or force a winner.

Public entry point: `int main(int argc, char** argv)` in `headless_match.cpp`.
CLI: `headless_match GAME_DIRECTORY FRAMES` (100 through 1000000).
No application API or Makefile changes are required.
Optional Makefile lines, with a literal tab before the command:

```make
.PHONY: headless-match
headless-match:
	tools/test_headless_match.sh
```
