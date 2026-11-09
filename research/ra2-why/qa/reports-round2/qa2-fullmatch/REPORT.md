# qa2-fullmatch (display 111) - build c1c79262
Result: match NOT finished. No VICTORY or score screen seen. ~35,000 game frames played (~30+ min). Game never crashed (alive at the end).
Setup: Allies/America, Easy, Game Speed = middle (the slider value I picked; I did not write down the exact label, so speed is unverified). Skirmish setup screenshots s02-s07.

## What I did
1. Menu -> skirmish setup -> start (s01-s08). Start: MCV + 10 Grizzly tanks (s09).
2. Deployed MCV (s10). Built power plant, barracks, refinery, war factory (s11-s25). Free miner came with refinery (s19).
3. Built tanks and GIs (s29). Scouted with arrow keys and found the Soviet base (s61).
4. Sent a small group. They destroyed the enemy construction yard and several buildings (s65-s66) but then died (s68).
5. Pressed H to go back to base (s69). Tried a second attack; I got lost on the map and ran out of effort (s70-s74).

## Bugs / differences
- MAJOR: No Radar Tower in the build list. Build list shows only Power Plant, Barracks, Ore Refinery, War Factory, Pillbox (s30). Radar is missing, so there is no minimap. Original RA2 has radar and minimap. Navigating a big map was very hard because of this. I could not finish the scenario step "radar".
- MAJOR: Credits stayed fixed at about $18,625-$19,125 and queue seemed stalled for several minutes while the miners worked (s63-s70). Also credits rose by about $500 per step earlier (s31-s47). Not fully verified; may be a stuck queue after "QUEUE FULL: FIVE PER TYPE" message (s48).
- MINOR: Easy AI base is far, map is very large with big water areas and arrow scroll overshoots easily (hold Left 2500 ms jumps across the map; s31-s33, s53). Scroll is too fast for hold-key scrolling.
- MINOR: Message "QUEUE FULL: FIVE PER TYPE" (s48) - original allows more; low priority.
- MINOR: Selected-unit list shows HP numbers (e.g. "GRIZZLY BATTLE TANK 300") instead of the original sidebar; fine for debug but different.
- NOTE: Enemy Easy AI did not attack my base during ~30 min. I saw no Terror Drones. Tank crushing of infantry: NOT TESTED. Auto-fire: my units did return fire when idle near enemy (s65-s66). Explosions and burning wrecks drew correctly (s66).
- Whole enemy conyard and big buildings vanished within 8 s (s66->s67). Either very strong damage, or the AI sold them. Not certain.
- Unit commands: left click on far ground moved units correctly across the map. Click on enemy building to attack worked (s65).
- Hotkey H centers on base: works (s69). Home key: no effect.

## Round-1 items
Not re-tested individually (I had no access to MERGED.md in this run). All: NOT TESTED.

## Worked well
Menu and setup flow; MCV deploy; sidebar tabs; placing buildings by click; "too far from base" message on placement (s13); building models and tank sprites look right; combat explosions; no crash/freeze.

## Not done
Victory, score screen, Terror Drones, crush, defend-attack test (no enemy attack seen), radar.
Debug log copy: debug-loop.log in this folder. Timing: 8 s wait = about 500 frames early on (s12 check: frame 992 -> 1495), so about 60 frames per real second.
