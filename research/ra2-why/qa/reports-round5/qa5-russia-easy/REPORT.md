# QA5 report: qa5-russia-easy (display 162), build d6345a1f
Scenario: Russia vs Easy AI, skirmish from main menu. Game Speed: I did NOT confirm the value 3. I used the default I saw (not changed).
Played to about frame 19000 (~15 min game time). I did NOT win. No score screen seen. Reason: BLOCKER below.

## Steps
1. Main menu, Skirmish, Russia, Easy, start. Screenshots m0-m4, g0.
2. Start army: MCV + 4 Rhinos + 12 Conscripts seen (g1). MCV deployed. Built Power, Barracks, Refinery, War Factory, Radar, Sentry Guns.
3. Right-click on cameo = hold, right-click again = cancel (h1, h2). Worked.
4. Placed 2 Sentry Guns near yard. Kept 4 Rhinos idle next to yard.
5. Enemy came at frame ~9065 (log). Fights logged. Radar built (rr3). Minimap shown.
6. Sent army west to enemy base found on minimap (mm2). Units would not move (b1-b8).

## Bugs
### BLOCKER: Conscripts and Rhinos ignore move/attack orders in the north-east field
Steps: finish Radar, drag-select army, click far west ground or enemy building, also tried single unit, `s` key, right-click deselect first.
Expected: units walk. Seen: orders show a cursor mark and "MOVING." but nobody moves for 60+ s (frame 15240 to 19000). Only a Harvester moved. Game kept running (frames advance, ./qa alive = running). Screens: at3, at5, at6, b1-b4, b8.
Result: I could not reach the enemy, so no victory and no score screen. Could be a stuck-pathing or unit-stacking problem: 30 Conscripts stand in one tight grid at the yard edge (sc1, at5).
### MAJOR? Enemy first shot early
First Easy attack unit shot at frame 9181 (E1 at my Construction Yard). Original Easy ~8.8 min = ~11100 frames at 21 fps. So ~17% early. My Rhinos shot first at frame 9065 (they auto-engaged). Round-4 item "too early": STILL SLIGHTLY EARLY (minor).
### Units block placement message: 
Not tested.
### Sidebar queue
I clicked a cameo 6 times (Rhino): queue badge showed 6 then 4 (b5, b6). Queue of 30: I clicked 31 times on a cameo in q1 but did not read the count clearly. NOT TESTED reliably.
### Minor: Sentry Gun / Pillbox
Sentry Gun (NALASR, weapon Vulcan) fired on its own at an enemy Medium Tank from frame 11412, 15 shots in log. Pillbox not built. Sentry auto-fire: FIXED.

## Round-4 re-test list
- Sentry Gun auto-fire: FIXED (log lines frame 11412-11619).
- Pillbox auto-fire: NOT TESTED (Soviets have no Pillbox).
- Idle tanks defend nearby buildings: FIXED (HTNK shot E1 at frame 9065 near yard, before E1 hit the yard).
- Enemy first attack too early: STILL ~17% EARLY (frame 9181 vs ~11100).
- Arrow scroll too fast: hold Left 500 ms moved the view about one screen left. Looked fast but not measured. NOT CONCLUSIVE.
- Minimap click over water: clicking the minimap moved the view (mm2, at5). Not tested on water specifically. NOT TESTED.
- No message when units block placement: NOT TESTED.
- Score screen: NOT TESTED (no win).
- Queue of 30: NOT TESTED reliably.
- Unit stacking: STILL BROKEN or related: 30 Conscripts sit in one dense grid, can not leave.

## Worked well
Menu flow, build placement preview (green/red diamond), hold/cancel on cameo, Esc pause menu (esc1), minimap appears after Radar, combat log lines, Sentry Gun and Rhino auto-defence.
## Not done
Win, score screen, F1 help, time/frame measures of build times.
Screenshots are in this folder (m*, g*, h*, s*, r*, w*, p*, t*, u*, sc*, hv*, rr*, mm*, at*, b*, esc1).
