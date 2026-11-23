# QA report: qa-flows (display 105), build 87292f36
Scenario: menus, game flow, stability. No crash, no freeze seen. Game stayed alive in all 3 matches (`./qa alive` = running every check). Debug-loop log: /tmp/ra2-debug-loop.F94jx3 (last lines were normal present begin/end pairs). Total play ~16 min over 3 matches (less than the 25 min asked).

## Steps
1. Start at main menu (01.png). Single Player -> menu with New Campaign, Load Saved Game, Skirmish (02.png).
2. Clicked New Campaign and Load Saved Game: nothing happens (03.png, 04.png identical).
3. Skirmish setup (05.png). Opened country list (06), colour list (07), difficulty list (08), Customize Battle (09), toggles, credits slider (11-13).
4. Match 1: America, blue, Easy, Isle of War, 5000 credits (14-24). Deployed MCV, built power plant, started refinery, moved tanks, scrolled. After ~5 min pressed Esc -> back to setup (25).
5. Match 2: Korea, red, Brutal, Urban Rush, 9600 credits (26-34). Built power plant and barracks. Esc after ~4 min (35).
6. Match 3: Germany, yellow, Medium, Pinch Point, 6500 credits, Short Game + Superweapons on (36-38). Esc after ~2 min.
7. Back to main menu. Clicked Internet, Network, Movies & Credits, Options (all 4): nothing happens. Exit Game: confirm box (41), Cancel works (42), OK exits cleanly.

## Bugs
1. **major** - Unit Count and Game Speed controls are missing on the skirmish setup screen (only labels, 05.png). Every match started with the same 9 Grizzly tanks + 1 MCV (15, 28, 37), so unit count cannot be changed. Expected: both sliders, with start units following Unit Count. Repro: Single Player > Skirmish; look at the Game Speed / Unit Count rows; drag at x 505-540, y 329/385: no effect.
2. **major** - No start-position choice. Position boxes show "???" for all slots and the list cannot be changed in a useful way; player 1 always starts in the same spot. (05.png, 07b.png). Expected: numbered start positions plus map preview.
3. **major** - Options, Movies & Credits, Internet, Network, New Campaign, Load Saved Game do nothing when clicked (dim text; screens identical, 03/04, 40). Expected: Options screen (sound, video, game settings) and Movies & Credits submenu. (Internet/Network may be intentionally off for a base-game rebuild; Options and Movies/Credits are in the brief.)
4. **minor** - Chosen colour is not used in game. Colour set to blue, red, yellow, but the player's units are always the same purple-red and the minimap dot is always green (16, 29, 38 vs 07/26/36). Expected: the chosen colour on units, buildings and radar.
5. **minor** - Esc in game leaves at once to the setup screen with no pause menu or "Abort/Quit?" question (25, 35). Easy to lose a match by accident. Expected: Options/Abort dialog. Settings are kept, which is good.
6. **minor** - Unit movement is very slow and untidy. 8 Grizzlies sent to a point (22-24) took 50 s and moved ~100 px; some drifted back toward the base, and they did not group at the click. Expected: tanks drive straight to the target in a few seconds.
7. **minor** - Production is slow in real time: power plant ~30 s, ore refinery still not ready after 2.5 min in match 1 (21-24, Easy, 5000 credits) and credits barely dropped ($3606 after 3 min). Could not tell if the build was stuck or just slow; barracks in match 2 took ~60 s. Check cost/time against original (refinery build normally ~40 s at normal speed).
8. **polish** - Slider tracks (credits, game speed, unit count) are invisible; only the small red handle shows (12). Credits max reachable was 9600 with my drag, not a clean 10000 step.
9. **polish** - Setup screen shows no map name or preview after Customize Battle > Use Map (10, 36). Cannot see which map is picked. Also nothing says the AI is "Brutal" vs "Hard" (list is Easy/Medium/Brutal, which matches the original).
10. **polish** - Bright cyan blobs on the maps (28, 37) look like wrong-palette ore/tiberium trees. The log says "TODO-ORIGINAL: Terrain TIBTRE01 ... unresolved", so it is a known gap.
11. **polish** - Placing a building too far from the base only shows "PLACE THIS BUILDING CLOSER" in a small status line (31); no red/green placement preview seen.

## Worked well
- Menu buttons that are active respond at once; Back/Cancel/Main Menu return correctly.
- Dropdowns (country, colour, difficulty) open, select and close with Esc.
- Chosen settings were applied correctly: credits 5000 / 9600 / 6500 appeared in game; maps matched the pick (Isle of War, Urban Rush, Pinch Point); settings kept after Esc.
- Three matches in a row with no crash or freeze; screens updated after every input; Exit Game confirm (Cancel and OK) works and the process ends cleanly.
- MCV deploy (D), power plant / barracks / refinery build and place, scrolling, box select, F1 help on start all work. Sidebar shows G.I. and Pillbox after barracks.
- Allied faction content (Grizzly tanks, Allied buildings) appears for America/Korea/Germany.

## Not tested
- Match end (no win/loss reached), Brutal AI attack timing, superweapons, Short Game rule effect, country-specific units (all countries gave Allied start units; Russia/Cuba etc. not started).
