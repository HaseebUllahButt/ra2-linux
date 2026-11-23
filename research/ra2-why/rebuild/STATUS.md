# Rebuild status: base RA2 skirmish is playable

Updated 2026-11-10. Scope is base Red Alert 2 skirmish. Yuri's Revenge is out of scope for now.

## What works

A player can start a skirmish from the original menu, play it and reach the score screen.
Agent QA rounds 4 and 5 each ended with player victories and no crash.

- Menu and skirmish setup screen from the original dialogs. The match uses the chosen map,
  country, color, credits, unit count, game speed, opponents (one computer house for each open
  slot, each with its own difficulty), Short Game and Super Weapons.
- Original sidebar and command bar art, cameos, tabs, power bar, radar.
- Shroud: the map starts black and opens around units and buildings.
- Original Esc options screen (Abort Mission, Quit, Resume Mission).
- Deploy, production queues, placement with the original cursor grid, power, low power.
- Movement, group moves, combat with turrets, bursts and prone infantry, guard and return fire.
- Harvesting with original load and unload timing.
- Computer opponent from AI.INI: team timers, trigger conditions, base building, attack teams.
- Victory by the original rule of game.exe 004E4110, and the score screen.

## How to check

From `rebuild/`:

- `make test` builds and runs every layer test. It must end with `== all layer tests passed ==`.
- `make human-input-check` clicks through the menu and plays a match with real input events.
- `make skirmish-check` runs headless matches: one enemy, and three houses on MP09T3.
- `RA2_BENCH=1 build/ra2-native --game-dir <game> --skirmish --headless --autoplay --frames N`
  prints time per phase.

The tests need the owned game files in `../game-full` or `../game` (not in git).

## Evidence sources

- `reference/REFERENCE.md`: measurements from the original game under Wine (build times, speeds,
  damage, computer attack times, sidebar layout), with screenshots.
- `export/` (not in git): decompiled game.exe, about 12,400 typed functions.
  `export/export/functions/<4 hex>/FUN_<address>.c` and `export/gap-sweep/`.
- `qa/`: the kit for agent play tests on hidden screens, and the reports of each QA round.

## Open items, most important first

1. Computer attack timing against the original. A Russian Medium computer first attacks at about
   frame 8,845 (headless opening check, MP02T2) with its "Nation Russian" team of Tesla Tanks.
   The old reference of about 16,500 frames likely had a computer that was not Russian. A like
   for like reference run is requested. Team scripts now run steps 53 and 54.
2. The computer builds each base building once. The original base plan (004EF890) adds extra
   refineries and defenses. Parked friendly vehicles do not move aside for a blocked unit; only
   foot soldiers do (original scatter 00476920). Economy ignores a miner that walls block.
3. A defeated house keeps its leftover units. The original destroys them (004E7DF0).
4. MP10S4 and maps like it: armies never meet (fired=0). Units cannot reach the enemy.
5. Setup options with no effect: Crates, MCV Repacks, Build Off Ally. Random country and color
   use a fixed rule, not the scenario random stream.
6. Esc screen: Load, Save, Delete and Game Controls are disabled. Save slots exist
   (`app/battle_saves`), but no match state is written.
7. Radar power-on animation: two RADAR.SHP frames draw about 14,000 white pixels. No capture of
   the original radar switching on exists yet, so the art is drawn unchanged.
8. Shroud edge is a soft fade; the original uses edge art (00475520). Map colors look brighter
   than the original.
9. Miner docking at the refinery, the purifier bonus, and threat scoring are simplified.
10. The cause of "no attack in 38 minutes against a player with only a Construction Yard" is not
    found. It is not in the trigger conditions (0041C990) or in script steps 53 and 0.
11. About 200 places in the code carry a `TODO-ORIGINAL` note. `grep -rn TODO-ORIGINAL rebuild/src`.

## Layers

| layer | state |
|---|---|
| fs (MIX archives) | done, crypto verified 3 ways |
| rules (INI) | done, real data |
| text (CSF) | done, 9,690 strings |
| map | done, 161 maps, full INI structures |
| pack (LCW) | done, verified on all maps |
| loop (game loop) | done, six phases |
| gfx (SHP/palette) | done, real tiles render |
| hook (DLL) | phase 3 proven (Bootstrap bridge-tested) |
| symbols | 8,584 / 8,584 named for gamemd.exe |
| skirmish match | playable; see the open items |
