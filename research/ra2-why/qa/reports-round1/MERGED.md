# Merged QA bug list, build 87292f36 (round 1)

Source: 5 QA agents playing like humans (qa-newplayer, qa-economy, qa-combat, qa-placement, qa-flows).
Reports and screenshots: /path/to/user/ra2-qa/reports/<name>/REPORT.md.
Stability: no crash and no freeze in about 2.5 hours of total play across 7 matches.

## Fix first (play-breaking)

1. **Production time is wrong and uneven.** First Ore Refinery took 4-7 min (credits drained only 4-18 $/s); a second refinery took 45 s; Radar ~2 min; War Factory several minutes; one GI ~60 s. Reactor and barracks took ~10 s. Compare with the recovered build-time code and the original money-drain rate. (economy #1, newplayer #2, flows #7, combat #8)
2. **Factory exit is broken.** Rhino tanks stay on the War Factory roof and stack there (economy #3). A GI spawns behind the Barracks art and does not walk out (combat #8).
3. **No build queue.** Four clicks on GI built one GI and charged once (combat #8). Original queues up to 5 per item, with a count on the cameo.
4. **Combat is invisible.** No visible shots, muzzle flashes or projectiles, no explosions, no death animations, no "unit lost" EVA. Units lose health and die with no visible attacker; two tanks "vanished" during an attack order (combat #1-#3).
5. **Unit movement is very slow and untidy.** 8 tanks took 50 s for about 100 px; some drift back; groups do not gather at the click point; tanks stack in one cell (flows #6, newplayer #9, combat #10).
6. **No War Miner or Chrono Miner cameo.** A second miner cannot be built (economy #2).
7. **Low power has no effect.** With power 0 the radar and minimap keep working, and build speed stays the same (economy #4).
8. **Cyan blobs over terrain.** Ore trees (TIBTRE01-03) or ore are drawn with the wrong palette or are unresolved (combat #5, flows #10).

## Fix next (major usability)

9. **Skirmish setup:** Unit Count and Game Speed sliders do not work and their tracks are invisible; start units ignore the count (always 9 Grizzlies + MCV); start positions show "???"; the chosen color is not used on units, buildings or the minimap; no map name or preview after choosing a map (flows #1, #2, #4, #8, #9).
10. **Esc leaves the match at once.** Original shows the Options / Abort dialog first (flows #5).
11. **Status line is stale and cut off.** "BUILDING GI." and "MOVING." stay for minutes; "BUILDING SOVIET ORE REFINE" and "ESC CAN" are cut at the edge; old text stays after a cancel (newplayer #4, economy #7, placement, combat #4).
12. **F1 help:** the help panel shows at start, but one tester saw F1 do nothing later (newplayer #1). Check that F1 toggles it at any time.
13. **New player guidance:** a sidebar box shows the word "DEPLOY" all game; the selected-unit list ("GRIZZLY BATTLE TANK 300" lines) has no title; the power text "200/60" has no label and no power bar like the original (newplayer #3, #5, #6).

## Polish

14. Placement footprint is a thin outline, not filled tiles, and sits to the lower right of the cursor (placement).
15. Arrow-key scrolling is too fast (combat #6, newplayer #12).
16. Enemy health bars always show; the original shows them on hover or selection (combat #11).
17. Miners drive far away instead of to the nearest ore; credits jump in big lumps (economy #5, #6; known TODO: unload timing).
18. Easy AI never attacked in 12+ min (newplayer #10). Check against the original Easy timing.
19. Sidebar tabs need a clear highlight; cameos show no cost on hover (newplayer #8, economy #7).
20. Options, Movies & Credits, New Campaign, Load Saved Game do nothing. Out of scope for the skirmish round unless cheap; Options (game speed, scroll speed, volume) matters most.

## Retest needed

- First queued GI never appeared (placement).
- Not covered yet: crushing, unit-vs-unit fights watched up close, auto-fire, map edge, cliffs, zero money, a full match to victory.
