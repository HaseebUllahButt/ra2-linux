# QA4 report: qa4-russia (display 142)

Build 59572f27. Russia vs Easy AI. Game Speed slider: I set the middle of the slider (shown as 3 on a 0-6 scale; I did not capture the exact label in a kept screenshot). Result: VICTORY, score screen reached (s69.png).
Match time on score screen: example.invalid. Last game frame: 25581 (about 30 frames per second of score time, not 21).
The game never crashed (`./qa alive` = running at the end).

Note: I deleted screenshots s01-s19 by mistake (menu, setup, first builds). Cited shots start at s20.

## What I did
1. Main menu -> Skirmish -> Russia, Easy AI, speed middle -> start.
2. Deployed the MCV. Built Reactor, Barracks, Ore Refinery, War Factory, Sentry Gun (x2), Radar, more Reactors, War Miner, Rhinos, Conscripts.
3. Frame ~4400: first enemy Grizzlies arrived. My 5 Rhinos killed them (log lines below).
4. Frame ~6000-8000: enemy GIs and Grizzlies attacked again at the Reactor. Rhinos killed them.
5. Built 2 Sentry Guns (one near base, one north). Watched them for the rest of the match.
6. Built 15 Rhinos + Conscripts. Marched west with the minimap.
7. While the army was away, GIs attacked my Construction Yard. I sent home Rhinos and Conscripts back. Yard survived (hp 754 -> 319).
8. Army destroyed the enemy Construction Yard, Barracks, Power plant, Refinery, War Factory. Last kill: an enemy miner. Score screen "You Are Victorious!".

## Bugs

### B1 - MAJOR - Sentry Guns never fire
- Steps: build a Sentry Gun, place near base. Let enemy units attack it.
- Expected (RA2): the Sentry Gun shoots enemies in range on its own, with visible shots.
- Happened: across the whole match the game.log has 0 shot lines with `type=NALASR` as the source. 76 shot lines have a Sentry Gun as target. 4 GIs shot one Sentry Gun from about 4 cells away and killed it, and it never answered (frames 20132-20154). Another Sentry Gun was shot by Grizzlies (frames 14854-14969, then died at 23051) and stayed silent. No shots or muzzle flash seen in screenshots.
- Evidence: sentry_lost_to_gi.png (message "SENTRY GUN LOST TO GI."), s39.png, game.log.txt.
- Log lines (kill of my Sentry Gun):
  - `combat event=death frame=20154 ... source=312 type=E1 house=2 weapon=M60 ... target=287 target_type=NALASR target_house=1 hp_before=5 hp_after=0`
  - `combat event=death frame=23051 ... source=26 type=MTNK house=2 weapon=105mm ... target=290 target_type=NALASR target_house=1 hp_before=23 hp_after=0`
- Possible link: low power? Power was 99 out / 135 use at one time, but at other times 249 out / 135 use and still no fire.

### B2 - MAJOR - Enemy attacks far earlier than original
- Expected: Easy AI first attack at about 8.9 min.
- Happened: first combat shot at frame 4413 (about 3.5 min at 21 fps / 2.5 min at 30 fps). 4 Grizzlies came with the first wave. Repeat waves at frames ~6000, ~7800, ~14800, ~20000, ~23000.
- Evidence: game.log.txt first `event=shot` line, s21.png, s22.png.

### B3 - MINOR - Score screen: Harvested is 0 for both sides
- Match had a War Miner that mined ($ went from $4790 up to $12125). Score screen shows `Harvested 0` for Player and Computer.
- Also the small "Kills" table shows Player 0 / Computer 32 (and Computer 5 / 0), which reads like swapped or mislabelled rows.
- Evidence: s69.png.

### B4 - MINOR - Frame rate / timer does not match 21 fps at middle speed
- 25581 frames at 14:12 on the score screen = about 30 frames per second. The task note says middle speed is about 21. Either the timer or the speed value is off. Not sure which.

### B5 - POLISH - Base defence order not automatic for Rhinos left at home
- New Rhinos from the War Factory sat idle while 4 GIs shot the Construction Yard. Units did not engage until I selected and ordered them. (In the original, idle tanks fire at enemies in range.) Not 100% sure this is a bug: the GIs may have been out of their sight range at first, but they were about 6 cells away and hit the Yard for 40 seconds.
- Evidence: s53.png, s54.png, s55.png.

## Kill log: who shoots my units (copied from game.log)
Losses on score screen: 5. All 5 are in my_losses.txt.
1. Rhino (id 15) killed at frame 4595 by enemy Grizzly id 17: `source=17 type=MTNK weapon=105mm target=15 target_type=HTNK hp_before=10 hp_after=0`
2. Sentry Gun killed frame 20154 by GI id 312 (above).
3. Tesla Reactor (NAPOWR id 266) killed frame 20513 by GI id 313: `weapon=M60 target=266 target_type=NAPOWR hp_before=2 hp_after=0`
4. Sentry Gun id 290 killed frame 23051 by Grizzly id 26 (above).
5. Rhino id 13 killed frame 23952 at the enemy base by GI id 297: `source=297 type=E1 weapon=M60 target=13 target_type=HTNK hp_before=1 hp_after=0` (earlier `hp_before=4 hp_after=1` at frame 23931).
Construction Yard (NACNST id 264) was shot by GIs 311, 312, 313, 315 (frames 21002-21814): e.g. `source=312 type=E1 ... target=264 target_type=NACNST hp_before=754 hp_after=751`. It fell to 319 hp and survived. I killed all four GIs (ids 313, 312, 311, 315) by frame 21829. Attacker was always findable in the log.
Also: my Harvester-type unit (HARV, id 284) shows as the source of `weapon=20mmRapid` shots at frame 7832+. The War Miner fired at enemy Grizzlies. This matches the original (the War Miner has a gun).

## What worked well
- The new combat log is clear: source id, type, weapon, target, hp before and after. Easy to find every attacker.
- "UNDER ATTACK" and "LOST TO" messages show on the sidebar, with a hotkey (H: base).
- Minimap jump works (click = move view). Army kept orders while I looked elsewhere.
- Build queue and "READY" labels work. Cameos for unavailable items dim correctly. Place-by-click works.
- Rhinos killed Grizzlies and GIs well. Enemy buildings died one by one: Construction Yard, Barracks, Power plant, Refinery, War Factory.
- Victory triggered when the last enemy unit (a miner) died, and the score screen appeared with "Esc: Main Menu / Click: View Battlefield / Tab: Score".
- No crash, no freeze in 14 minutes of match time.

## Re-test of earlier items
Round 1-3 lists were not re-read line by line. I only saw these in play: place-by-click building (FIXED/works), sidebar queue stacking (works), pause/help not tested (NOT TESTED), score screen (works).

## Files
Screens s20-s69.png, sentry_lost_to_gi.png, game.log.txt, my_losses.txt, debug-loop-tail.txt (last 2000 lines).
