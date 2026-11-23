# QA report: qa-newplayer (display 101), build 87292f36

Limits: I played about 45 min real time. I did NOT destroy the enemy. The game never crashed.
Note: the screenshots of the menu steps (s01-s13) could not be re-viewed by me. My notes on the menu and the first power plant are less exact. Later screenshots (s29 and up) I saw.

## What I did
1. `./qa start 101`. Main menu > Single Player > Skirmish. Picked Allies (America), Easy, Start.
2. Used the sidebar tabs (ALL / BLD / INF / VEH / AIR). Built Power Plant, Refinery, Barracks, War Factory (placed each on the map when READY).
3. Built G.I.s at the Barracks. Selected the start tanks with a drag box, clicked the minimap, clicked the ground to move them.
4. Waited about 8 minutes while the tanks crossed to the Soviet base (top left of the map). Checked `./qa alive` often: always "running".
5. Pressed F1 twice (s56): nothing visible happened.

## Bugs / differences
1. **major** - F1 shows no help panel. Steps: in game press F1, screenshot. Expected: help screen (task text says F1 gives help). Got: no change. File: s56.png. A new player has no help at all.
2. **major** - Very slow production, no feedback. Refinery needed ~4+ min of real time; War Factory also several minutes (s14-s41). The sidebar showed no progress bar/clock I could see on the item. Expected: clock-wipe on the icon and faster build with 7000+ credits. Files: s17, s18, s19, s31.
3. **major** - A new player cannot tell which army is theirs or what to do next. The map starts with a big group of tanks next to the base; no text tells the player they are selectable or what they are. Sidebar bottom shows the word "DEPLOY" (no meaning) all game. Files: s29, s30.
4. **major** - Sidebar status text is stuck and unclear. "BUILDING GI." stayed for the whole game, even when nothing was building (s29-s47). "MOVING." stayed for minutes after the units stopped (s53-s55). Expected: status goes away or changes. 
5. **minor** - Three lines "GRIZZLY BATTLE TANK 300" are shown in the lower sidebar (s48-s57) with no title or explanation. I could not tell if it is a selection list, a queue or a bug.
6. **minor** - Power display "POWER 200/60" then "200/85" has no label for which number is made and which is used. Original RA2 uses a bar with a clear low-power state.
7. **minor** - Placing a building: after clicking READY the status says "PLACE ALLIED WAR FACTORY." Clicking near my buildings gives "CANNOT DEPLOY HERE" (s43) but no reason is shown (the red grid is the only hint). Clicking the READY icon again while a placement is active did not cancel placement.
8. **minor** - Tab clicks reset: when I click BLD/INF the list changes, but "ALL" shows a mixed list including units; the Pillbox and G.I. tiles look fine. Tabs are not highlighted clearly (s33/s34 thin yellow box only). Polish.
9. **major (unconfirmed cause)** - The tank army moved very slowly and unevenly. After ~8 min only 1-2 tanks reached the enemy base (s62); the rest were still on the minimap in the middle (mm61.png). I did not check them closely, so I cannot say if they got stuck.
10. **minor** - Enemy (Easy AI) at ~12+ minutes of game time still has its whole tank group parked at its base and never attacked me (s48, s62). Not like the original, where the AI attacks. May be OK for "Easy".
11. **polish** - Credits kept rising to $10400 (s62) with me spending little; hard to see that a harvester works. Start money looks very high.
12. **polish** - Arrow-key scrolling via `hold Left/Up` moved the view a long way fast (s50). Not checked against original speed.

## What worked well
- Menu path to Skirmish worked, and the match started.
- Sprites, terrain, buildings and the sidebar art look like original RA2. Minimap shows both armies in green/red dots.
- Placing buildings with the red grid and "BUILDING PLACED." message works; power went from 200/60 to 200/85 after the new building.
- Drag-box select, minimap click to jump the view, click-to-move all worked.
- No crash, no freeze. Debug loop kept presenting frames the whole time (copied: debug-loop.log, qa-log.txt).

## Not done
- I did not destroy the enemy or test combat. I did not test sell, repair, rally points, hotkeys, or the other sides.

## Screenshots cited
s17.png s18.png s19.png s29.png s30.png s31.png s43.png s48.png s50.png s53.png s56.png s62.png mm61.png
