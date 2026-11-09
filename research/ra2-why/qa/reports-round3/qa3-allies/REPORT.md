# qa3-allies (display 132) - build 00065f43
Result: NO VICTORY, no score screen. Match not finished. Game never crashed or froze (alive at the end, ~17,000 frames, about 20 min real time).
Blocked by: (1) the radar building cannot be placed, so no minimap and no way to find the enemy; (2) my Construction Yard vanished at about frame 15000-17000, so I could not build anything more.
Setup: America, Easy AI, colour green. Game Speed slider range is 0-6; I set it to 3 (the middle value, screenshot s008). Time: 1 real second is about 60 frames.

## What I did
1. Menu -> skirmish setup (s001-s010), picked America, green, speed 3. Started.
2. Deployed MCV (s012). Built Power Plant, Refinery, Barracks, War Factory (s013-s034). Power Plant took about 700-1000 frames (s016-s017); Refinery about 2000 frames.
3. Built the Air Force Command HQ (AMRADR). Cameo is in the BLD list and builds (s035-s038) but placing it fails (BUG 1).
4. Queued 5 Grizzly tanks and 5 GIs at once; the queue works with a count on the cameo (s037, s050-s052). Army grew to about 20 tanks and 18 GIs (s071).
5. Scouted with arrow keys north, west, south and east (s053-s069). Found only water, a city, and a broken bridge. No enemy.
6. Waited 90 s more (s070-s071). Construction Yard gone, sidebar BLD tab empty (s072).

## Bugs
1. BLOCKER - Radar (Air Force Command HQ / AMRADR) cannot be placed. Steps: build the Power Plant, Barracks, Refinery first; in BLD tab build "Airforce Command"; when "ready", click it and click any ground (tried 8 spots, also next to the base). Expected: building is placed, radar and minimap work. Happened: green footprint shows, click gives "DOCK WALL AND OVERLAY / BUILDING UNLIMBO IS NOT RECOVERED"; the cameo stays "ready" forever. Shots: bug-afc-unlimbo.png, s044-s046.
   Note: the build time of it was OK (about 1500 frames). Also, the radar is the only AMRADR path; no other minimap source exists, so there was no minimap test.
2. BLOCKER - Construction Yard disappeared by itself. Steps: play to frame ~17000 with no enemy seen near the base. Expected: base stays (no enemy attacked). Happened: yard gone from the map, BLD tab empty, so no more buildings can be built. I did not see an explosion or an EVA message. May be an enemy attack I did not see (I was scouting away) - not sure. Shots: s052 (yard still there), bug-factory-missing.png and s072 (gone).
3. MAJOR - Without radar there is no minimap, and the map is huge. Scrolling blindly (arrow hold) overshoots over water in seconds. I could not find the Easy AI base in about 8 scouting attempts. Scroll is still too fast: hold 800 ms moves about 450 px (s053). Shots: s053-s069.
4. MAJOR - Miners wander far from base (NW grass, no ore visible) while ore patches are next to the refinery. Three miners seen far north-west (s071-s073) idle or walking. Expected: pick the nearest ore. Credits still rose (see below), so some mining works.
5. MINOR - Status line shows "BUILDING UNLIMBO IS NOT RECOVERED" in small text, developer wording.
6. MINOR - Sidebar page tabs ALL/BLD/INF/VEH/AIR work; ALL tab not tested for content. No Naval/other pages needed.
7. POLISH - Selected-units list still has no names, just a "SELECTED UNITS" label.

## Round-2 items
- Radar in sidebar for America (AMRADR): the cameo is there (FIXED for the list), but it cannot be placed (STILL BROKEN).
- Minimap after radar built and powered: NOT TESTED (radar could not be placed).
- Stuck build queue: FIXED, queue of 5 builds and finishes in order (s037, s050-s052).
- Stuck credits: FIXED, credits changed all game: $7700 (f5800), $7170 (f7566), $6465 (f12088), $9825 (s067), $14325 (f17078), $14825 (s072).
- Unit stacking after moves: Tanks and GIs leave factory/barracks in neat rows, not stacked (s053, s071). FIXED as far as seen.
- Miners choosing nearby ore: STILL BROKEN / doubtful (bug 4).
- Sidebar pages: work. Scroll speed: STILL TOO FAST (bug 3).
- Selected colour: FIXED. Green is used on buildings, units, selected list (s034, s053).
- Esc pause menu, F1 help: NOT TESTED. H key re-centres the view: works.
- Victory / score screen: NOT REACHED.

## Worked well
Menu and setup; MCV deploy; queueing 5 units with count; build speed reasonable; combat/enemy not seen; smooth running, no crash; green colour; placement footprint filled green/red with correct colours; "too far / blocked" feedback.
Debug-loop log: debug-loop.log.
