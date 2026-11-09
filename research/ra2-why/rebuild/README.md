# RA2 engine rebuild

Run the base Red Alert 2 skirmish prototype on native Linux.
The default menu starts a match with an Easy computer opponent.
The game reads owned assets. It does not run the Windows executable.

The rebuild remains incomplete. Matches use `PrototypeComplete` world state.
Campaigns, saves, and multiplayer are not complete game modes.
Keep later modules separate from base skirmish play.

The computer reads teams, task forces, and trigger choices from base `AI.INI`.
Difficulty controls team choices and delays. Supported team plans include movement,
attack, guard, and repeat. Several other script actions remain incomplete.
TODO-ORIGINAL: Team random choices and some timing still use prototype behavior.

Build with a C++17 compiler, make, pkg-config, SDL2 development files,
and the LZO2 runtime library. On test-host, owned assets are in `game-full/`.
Tests select that folder when it contains `game.exe`.
Set `TEST_GAME_DIR` to use another asset folder.

Run from the project root:

```sh
make -C rebuild -j8 test
make -C rebuild human-input-check FEATURE_GAME_DIR="$PWD/game-full"
rebuild/build/ra2-native --game-dir "$PWD/game-full"
rebuild/build/ra2-native --game-dir "$PWD/game-full" --skirmish --headless --autoplay --frames 12000
```

Choose Single Player, Skirmish, and Start Game in the menu.
Close the help panel with a click or F1.
Select the MCV and press D, double-click it, or click Deploy.
Choose a building cameo. Click READY, then click a green footprint to place it.
Units and buildings block placement. Red footprints show a blocked or distant position.
Drag a box to select units. Left-click the ground to move or an enemy to attack.
Right-click to clear selection. Press S to stop selected units.
Move the camera with arrow keys or the screen edges. Click the minimap to jump.
F1 shows controls and pauses the match. Esc cancels placement or returns to the menu.
The score screen appears at match end. Click it to return to the battlefield.

The human check sends SDL events through menu Start and the normal window event queue.
It writes private captures under `/tmp/ra2-human-input.*`.
Set `RA2_INPUT_DEPLOY=key` or `sidebar` to test help and focus around deployment.
Set `RA2_DEBUG_LOOP=1` to write input and presentation timestamps to a private
`/tmp/ra2-debug-loop.*` file. The process prints the exact path to stderr.

## Why

The game is a 32-bit Windows binary from 2000/2001. It needs shims to
run today. When the shims stop working, the game dies with them.
A rebuild keeps the game alive on any future machine.

## Method

1. Export every function (Ghidra headless, done once, refreshed as
   analysis improves) into rebuild/decomp/ - one C file per function.
2. Name functions and structs from the community knowledge base
   (YR++ and Ares headers document gamd 1.1.001 addresses).
3. Retype and clean function by function, starting with the core
   loop, the file system, and the rules system.
4. Build the native Linux replacement in `src/`. Keep the older Windows hook
   experiments in `hook/` outside the native executable.
5. Verify by behavior, not bytes: same maps, same skirmish AI,
   same multiplayer events.

## Legal reality, stated plainly

Decompiled code is a derivative work of EA's copyrighted binary.
This rebuild stays private, on this machine, using an owned copy
of the game. If it ever goes public, the safe paths are: EA's
permission, or a clean-room rewrite of the final code by people
who never saw the decompilation. Community precedent shows
open decompilation projects exist, but the legal risk sits with
distribution, not with analysis.

## Roadmap

Phase 0 (this session): full decompile export, symbol database
seeded from community data, codex jobs for cross-check and
loop/network docs.
Phase 1: name the file system (MIX) and rules layers completely.
Phase 2: retype core structs (Object, House, Unit, Building).
Phase 3: hook DLL that runs against the original exe.
Phase 4: full replacement binary.
