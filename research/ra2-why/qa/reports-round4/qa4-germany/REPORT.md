# qa4-germany (display 141) - build 59572f27
Result: NO VICTORY, no score screen. Match not finished. Game never crashed or froze (alive at end, frame ~28500, about 40 min real time).
Setup: Germany, Easy AI, Game Speed slider = 3 (middle; slider 0-6, shot s05), 10000 credits. Radar = Air Force Command (GAAIRC cameo "AIRFORCE COMMAND").
Why no victory: I built a base slowly (queue/placement waste), met the first enemy attack ~frame 13000, then at ~frame 25000 a big Rhino-tank attack plus my own attack on the enemy base killed most of my army (see my-losses.log). I ran out of time/credits. I reached the enemy base (Soviet, s72-s76): the enemy yard/structures were damaged but the match did not end.

## What I did
1. Menu, setup (Germany, speed 3), start (s01-s06). Deployed MCV, built Power Plant, Refinery, Barracks, War Factory (s07-s34).
2. Air Force Command built (~frame 19300) and placed (s60-s61). Minimap works afterwards (s66, mm crop): shows my blue dots and the green enemy base.
3. Pillboxes x2 at the west edge (s61). Tanks queued in counts (s36-s38, s51).
4. First attack: Shock Troopers (SHK) at frame ~13050-13700 (log lines below) and Rhino tanks later. Pillbox under attack message shown ("PILL BOX UNDER ATTACK. H: BASE") ok.
5. Army (~14 tanks + GIs) sent to enemy base (s69-s76); fought; lost most units.

## Bugs
1. MAJOR - Army wiped out and match not winnable by normal play? Not a bug by itself. Not counted.
2. MAJOR - Enemy first attack came at frame ~13050 = about 10.3 min at 21 fps (original ~8.9 min). Close; minor difference. (log: first shock trooper shots frame 13283.) POLISH.
3. MAJOR - Miners: a Chrono Miner was seen far to the north-west, in grass with no ore (s52 at frame 15148, s67, s68), while ore sits beside the refinery. Steps: play to frame ~15000, look NW of base. Expected: miner works the nearest ore. Happened: miner idle/away on plain grass. Screens s52, s67.
4. MAJOR - Scroll too fast: hold Left 300-500 ms moves the view by hundreds of pixels (s30, s62). Expected: gentle. STILL TOO FAST.
5. MAJOR - Large area of the map top-left is covered by thousands of coloured dots/flower-like tiles (s52 z4 crop: glitchy rainbow dots). Looks like a bad texture/overlay (tiberium-like gems?) Possibly ore gem drawing. Might be an ore field drawn as noise. Screens s52, s57.
6. MAJOR - Minimap click jumps view to the corner/edge: clicking minimap at (880,713) put the view on the map's far SE water (s54) and clicking (852,714) put view at the enemy base - ok, but one click moved off-map into water (s54, s56). Expected: view clamps to map. Actually the water is part of the map; unclear. MINOR.
7. MINOR - Placing buildings: units standing in the footprint block placement with no message (s58). Original shows red cells too; message missing.
8. MINOR - Credits drop shown but a ready building cameo stays "ready" while I was building other things; Air Force Command cameo stayed "1" after placing for several frames (s61 shows gone, ok).
9. POLISH - Selected units list is just a text list without names for buildings (shows only first 3 units).
10. POLISH - Sidebar cameo "Chrono Miner" shows a count badge without clear price; fine.
11. MAJOR(?) - I was unable to see every shooter/shot clearly: enemy Shock Troopers' ElectricBolt (s48) was visible, but Rhino tank shells at range were not visible in the screenshots I took; log has them. Not proven; NOT CONFIRMED.

(Honest note: bugs 2, 6, 7, 9-11 are weak/unconfirmed; solid ones are 3, 4, 5.)

## Round items
- Radar (Air Force Command GAAIRC) placeable: FIXED (s60-s61). Minimap after radar: FIXED/works (s66).
- Stuck queue / credits: FIXED (credits change; queue of 5-6 tanks works, s51, s67).
- Cameos staying in place: FIXED (unavailable cameos are dimmed, slots stay; s56-s61).
- Miners using nearby ore: STILL BROKEN (bug 3).
- Scroll speed: STILL TOO FAST (bug 4).
- Esc pause menu: tested Escape in play (s46-s47): game kept running, no pause menu seen -> may be cancel of selection; NOT CONFIRMED. F1 NOT TESTED.
- Ore drawing: ore patches near refinery drawn OK (s55); top-left field drawn as odd dots (bug 5).
- Victory/score screen: NOT REACHED.

## My losses (copy of log, full file my-losses.log)
Pillbox GAPILE (id 274) died frame 15349 to Rhino HTNK 120mm. Pillbox GAPILL (279) died frame 20281 to Conscript E2 M1Carbine. Tank destroyer, 10 Grizzly MTNK and 5 GIs died frames 25307-27118, killers: Rhino HTNK 120mm (ids 20-26).
Example: combat event=death frame=25528 source=25 type=HTNK house=2 weapon=120mm target=14 target_type=MTNK target_house=1 hp_before=30 hp_after=0

## Worked well
Menu/setup; MCV deploy; building queue counts; placement footprint green/red; radar + minimap with unit dots; pillbox under-attack message; combat log lines clear (who shot/killed whom); no crash, no freeze; cameos dimmed in place; shock trooper and tank shots visible.
Frame timing: Power plant ~700 frames; frame 1300 at ~64 s real => about 20 fps game rate.
Screens: s52 (miner in grass), s57/s58 (placement blocked), s61 (pillbox attack message), s66 (minimap), s72-s76 (enemy base), s30/s62 (scroll).
