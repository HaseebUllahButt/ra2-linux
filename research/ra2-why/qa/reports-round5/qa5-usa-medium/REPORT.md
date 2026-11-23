# QA5 qa5-usa-medium (display 161) - build d6345a1f
Scenario: USA vs Medium AI, main menu, Game Speed 3 of 0-6 (middle). Result: VICTORY, score screen reached.
Game frames at end ~25417 (log). Screens: s01..s86.png, score.png. Log: game.log (copy).

## Steps
1. Main menu > Skirmish, America, 1 Medium enemy, speed set to 3.
2. Deployed MCV. Built Power Plant, Barracks, Ore Refinery, War Factory, Radar, 2nd Refinery, 2 Pillboxes, a Service Depot-less opening (credits stayed ok).
3. Queued 30 GIs in one click run (clicked GI cameo 30x), later more GIs/Grizzlies.
4. Defended base with Pillboxes + idle tanks; then pushed with ~15 Grizzlies + GIs to enemy base (NW) and destroyed it. Last enemy unit (harvester) killed -> victory.

## Round-4 items
- Pillbox auto-fire: FIXED. GAPILL fired first at frame 10607 at the enemy Engineer/Conscript, and killed 12 enemy units total (log).
- Idle tanks defend nearby buildings: FIXED. Idle Grizzly (id 242) shot the intruder at frame 10594 with no order; enemy Flak units next to my base got shot.
- Enemy first attack too early: first enemy shot at my base frame ~10650 (Flak Track hits Grizzly). At 21 fps = 8.2 min. Original Medium ~13.2 min. STILL TOO EARLY (about 38% early). If real speed is ~17.5 fps it is ~10 min, still early.
- Arrow scroll too fast: FIXED / OK. 1 s hold scrolls roughly 150-200 px; usable, not jumpy.
- Minimap click over water: PARTLY. Clicking the minimap at the map corner showed open water in view (s47, s50) - view can go to water-only area. Not tested at the original clamp. Minor.
- No message when units block placement: NOT TESTED (placements all succeeded).
- Score screen: Time example.invalid, Kills 53/Losses 19, Built 90/10 vs 52/11, Harvested 27500 vs 19000 (score.png). See bug B1/B2.
- Queue of 30: FIXED. 30 clicks gave a queued count; sidebar badge showed the stack and all trained over time. Money drained as expected.
- Unit stacking: STILL BROKEN (minor). Grouped GIs/Grizzlies drawn overlapping in tight blob while idle (s45, s52, s67); the original spreads units.

## Bugs / differences
B1 (minor) Score time example.invalid does not match frames. 25417 frames / 21 fps = 20:10. Either the clock runs ~17.5 fps or time is miscounted. Expected: time = frames / game speed rate.
B2 (minor) Harvested shows round numbers (27500 / 19000), exact multiples of 500. Likely counted per load or rounded; original shows exact credits. Cannot prove, only odd.
B3 (minor) Enemy first attack early (see above).
B4 (polish) Score screen "Built 90/10" meaning unclear (units/buildings?). Compare to original "Units built / Structures built" labels - not checked.
B5 (polish) After victory the enemy last unit was a lone War Miner far away; no "all structures destroyed" auto-win until it died. This matches the original rule (all units and buildings), so OK.
B6 (minor) Radar/minimap click moved the view to the map corner and put units off screen for drag-select; drag-select only grabbed visible units (normal).

## Worked well
- Build times looked right (Power Plant, Barracks, Refinery, War Factory similar to table; checked by frame stamps 1195 -> 1301 etc).
- Pillbox kills, tank defence, combat log fine. No crash or freeze (alive always running).
- AI base fully destroyed with a Grizzly/GI force; victory screen and "You Are Victorious!" appeared.
- 30-deep queue worked.
