# QA report: qa-combat (display 103), build 87292f36
Time played: about 25 min real time. Game did not crash or freeze (./qa alive = running all the time).
Start: MCV + 2 Grizzly tanks. No infantry at start. The match did not end.

## What I did
1. Closed the help box (click). s1, s2.
2. Single click on a tank: selected (s3). Drag box over 2 tanks: both selected, MCV outside box not selected (s4).
3. Moved 2 tanks with left click on ground (s5). Pressed S (s6).
4. Held Left arrow 1.5 s (s7) and clicked minimap (s8, s9).
5. Selected MCV, pressed D: deployed (s10). Built power plant, barracks, placed them (s11-s15). Trained GI (s16-s22).
6. Selected tanks + GI by drag box and moved them (s23-s25). Clicked on a cliff (blocked cell, s25).
7. Moved across map to enemy base, then jumped the camera by minimap and left-clicked an enemy building with the group selected (s26-s31).
8. GI died inside enemy base (s31-s33). Searched for the tanks (s34-s37).

## Bugs / differences
1. **major - Tanks vanished without trace.** Steps: select 2 tanks + 1 GI, move them far toward enemy base, then jump camera by minimap and left-click an enemy building. Expected: tanks drive there and fight, or I can find them. Seen: only the GI arrived; tank entries left the selection list and I found no tank on the map or minimap (s34-s37). I did not see them die (no explosion, no message). Either they died unseen or got lost. Unsure which; needs dev check.
2. **major - Units get hurt with no visible attacker / no visible shooting.** Grizzly dropped 300->250 HP while idle near my base (s23). GI went 125->37 HP in the enemy base and died (s31, s32). I saw no shots, no muzzle flash, no enemy unit near it. Expected: visible attackers, firing effects.
3. **major - No death feedback.** The GI died with no death animation, no sound cue visible, no "unit lost" text. It just vanished (s31->s32).
4. **minor - Status text stale.** After the GI died, the sidebar showed GI portrait and "MOVING." (s33-s37). The selection list was empty but the text stayed.
5. **major (render) - Cyan blobs over terrain** on large areas of the map near cliffs and the city (s24-s29, s34). Probably missing tiles or ore/gem drawn wrong. Not in original RA2.
6. **minor - Arrow scroll too fast / big.** Holding Left for 1.5 s moved the view about one third of the map (s7). Hard to control.
7. **minor - Barracks placed overlapping power plant sprite** (s15). Placement was accepted at a spot where the building art overlaps. Footprint overlap not confirmed.
8. **minor - GI spawns behind the barracks art** (hidden by sprite), and does not walk out (s19). Also GI production was slow: one GI in about 60 s, and queueing 4 clicks produced only one GI and charged $200 once (s16-s20).
9. **minor - Click on a cliff move order:** units walk to the cliff foot and stop (s25). This is acceptable; original RA2 also refuses. No bug, noted only.
10. **polish - Tanks stack on top of each other** at destination after S/move (s6).
11. **polish - Enemy health bars always shown** (red bars over enemy buildings even at full HP, s30). In original RA2 bars show only on hover/select.

## Not tested (honest)
- Attacking a unit with left click, tank crushing infantry, infantry vs tanks, auto-fire at range: no fight reached in a way I could watch. Could not confirm any of it works.
- Right-click deselect, edge scrolling with mouse, double-click MCV deploy.
- Red dots on minimap moved toward the center at the end (s37): enemy force approaching, not seen directly.

## What worked well
- Single click select, drag box select, selection panel with names and HP.
- Move order with marker, pathing around cliffs and buildings.
- D deploys MCV. Building queue, ready, place with click.
- Arrow keys and minimap click scroll the camera. Minimap shows base and enemy dots.
- No crash, no freeze.
Screenshots: s1..s37.png, mm.png in this folder. Debug log copy: debug-loop.log.
