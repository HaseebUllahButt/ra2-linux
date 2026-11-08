# RA2 engine rebuild

Rebuild the original Red Alert 2 engine for native Linux. Base Red Alert 2 is
the main target. Keep Yuri's Revenge behavior separate where the games differ.

The game is incomplete. The Linux client shows original terrain, full-health
building main images, and standing infantry. It advances the recovered native
state. Full movement, combat, production, AI, campaigns, saves, multiplayer,
and menus still need work. The scene also needs pixel depth, shadows, lighting,
house colors, vehicle assembly, and other actor states.

Build with a C++17 compiler, make, pkg-config, SDL2 development files, and the
LZO2 runtime library. Keep the owned game assets in `game/` at the project root.
The native build reads original files. It does not run the Windows executable.

Run from this directory:

```sh
make native
make native-check
build/ra2-native --game-dir ../game --map ../analysis/extracted/maps/2PEAKS.MAP
```

Use `--yuri` to select expansion assets and rules. Use arrow keys to move the
temporary scene viewer. Press Escape to exit. These viewer controls do not
establish the original game input behavior. This is not yet a playable match.

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
