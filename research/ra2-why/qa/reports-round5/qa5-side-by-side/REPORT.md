# qa5-side-by-side report (native d6345a1f vs original game.exe)
Not a full 6-minute run. Native ran ~10000 frames (~8 min). The original lost time while its Esc menu stayed open. No fight happened, so no victory/score screen. Screenshots: `s/n_*.png` (native), `s/o_*.png` (original), same name = same moment.
Game speed 3 in both (native slider set by hand; original = saved setting). America vs Easy in both.

## Steps
1. Menu -> Single Player -> Skirmish in both (n_menu/o_menu, n_skirm/o_skirm).
2. Native: default map crashed on start ("Missing original game file: Round.MAP"). Restarted, picked another map. Original: Isle of War / default list. **Maps are NOT the same** (native shows water at the right edge, original is shown in a different area), so base layouts differ.
3. Deployed MCV, Power Plant, Refinery, Barracks, War Factory, 3 GI, 2 Grizzly, moved a tank 10 cells in both.

## Differences (native vs original)
1. **BLOCKER-ish (major) Default map fails**: native start with the default skirmish map quits at once: "Missing original game file: Round.MAP". Original starts it fine. (n_start, n_start2)
2. **Major – whole UI look**: original has the real RA2 sidebar: metal frame, radar eagle logo, credits at top, tab buttons, black unit grid, bottom command bar (o_s0, o_dep, o_g1). Native has a flat dark-green text panel: "CREDITS $", "POWER OUT/USE", tabs ALL/BLD/INF/VEH/AIR, text status line at the bottom, no radar/minimap, no bottom bar (n_s0, n_g1). Cameo order also differs: original tab shows only one category (buildings: Power Plant, Ore Refinery, Barracks, War Factory, Airforce Cmd, Naval Yard); native "ALL" shows everything mixed, in 2 columns of up to 5 rows and 2 pages.
3. **Major – fog of war**: original starts black with only a small lit area. Native shows the whole map, no shroud (n_s0 vs o_s0).
4. **Major – Esc menu**: original shows full menu (Game Controls, Load, Save, Delete, Abort, Resume). Native shows a small text box "GAME PAUSED / Resume / Abort" (n_esc vs o_esc). Native has no save/load/options.
5. **Major – building placement**: native needs the placing cursor on legal ground next to the base. I needed about 8 tries for the refinery and many for the war factory ("CAN'T BUILD HERE", n_r2..n_r5, n_w4). The original has a helper that auto-places; a human in the original also gets a green/white footprint. Native footprint is a red grid and is hard to read. Clicks on free-looking grass near the base were refused.
6. **Minor – start army**: both 5 Grizzlies; native 3 GI + start tanks; original a few more infantry. Native tanks are spread wider around the MCV (n_s1 vs o_s0).
7. **Minor – build speed (not measured exactly)**: Power Plant ready in ~25 s native, ~23 s original (n_pp2/o_pp2). Refinery and barracks look similar. War Factory ready ~60 s in both. GI/Grizzly queue works in native (n_g1 "QUEUED GI").
8. **Unit speed**: original 1.94 cells/s (track tool, 10 cells in 5.2 s). Native 10 cells: frames 7668 -> 7711+ for ~8 cells (n_t1..n_t9): about 7 frames per cell-and-a-bit, close to 2 cells/s at 21 fps. No clear difference.
9. **Minor – look of units**: native units are the same sprites but draw smaller/flatter selection (thin blue box vs the original green health bar + red move marker, o_st1 vs n_st1). Original shows move-order flash and shell craters (o_fin); native shows ore dots on terrain but no craters.
10. **Polish – fonts/colours**: native uses a pixel font in light green on dark, original uses metallic blue buttons with yellow text. Native colours of terrain are close to the original.
11. **Polish – sounds**: both muted, not compared.
12. Original Game Controls / sidebar tabs click ranges differ: native tabs at top (y~47), original tabs at y~210.

## Round-4 re-test
- Sentry/Pillbox auto-fire: NOT TESTED
- Idle tanks defending: NOT TESTED
- Enemy first attack too early: NOT TESTED (no enemy seen in ~8 min)
- Arrow scroll speed: NOT TESTED
- Minimap click over water: NOT TESTED (native has no minimap in view)
- Message when units block placement: NOT TESTED (message "CAN'T BUILD HERE." exists for bad spots)
- Score screen: NOT TESTED
- Queue of 30: NOT TESTED (queued 3 GI, 2 Grizzly fine)
- Unit stacking: NOT TESTED

## Worked well
- Deploy MCV, build chain, production queue with count badge, status line messages, Esc pause, unit move and speed look right.

## Notes
- Native frame count at end ~10082; no "combat event" lines.
- I could not match the map; compare with care.
