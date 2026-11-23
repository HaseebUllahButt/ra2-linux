# qa2-soviet report (display 112), build c1c79262
Setup: Russia, Medium Enemy, Game Speed 6 (slider max), Credits 10000, Unit Count 3, colour green. 3 matches, about 6 min of real play in total (shorter than the 25 min goal: every match lost the Construction Yard at 70-90 s, so I could not go further). Debug logs: debug-loop-run1/2/3.log in this folder. No crash, no freeze.

## Steps
1. Main menu > Single Player > Skirmish. Set Medium Enemy, green colour, speed 6, unit count 3 (s03-s07). Start (s08).
2. Help panel shows at start (s08). Click closes it. Deploy MCV with D. Build Reactor, Barracks, Ore Refinery, Sentry Guns, Conscripts, War Factory (s10-s23, w1-w7, y1-y2).
3. In each match the Construction Yard disappeared about 70-90 s after start (s16, t5, w7/x2, y3). After that nothing can be built.
4. Tested F1 (v1), Esc pause (v2), Resume (v3), Abort (z1, z2).

## Bugs
1. **BLOCKER - Construction Yard dies at about 70-90 s, every match (3 of 3), Medium AI.** Repro: start with the steps above, build Reactor then Refinery, wait. Expected: Medium AI does not attack for several minutes; a yard has 1000+ hp and visible combat. Got: yard vanishes in a few seconds with no shots, no explosion, no EVA; the queued building is refunded; buildings and Sentry Guns die the same way (Refinery and 2 Sentry Guns also gone in w7 -> x1, y3 -> y4). Blue enemy tanks (3) seen at map west edge just before (t5 left side, w7 top-left). My 3 Rhinos near the yard did not stop it. Files s15-s19, t1-t8, w7, x1, x2, y3, y4. Result: War Factory, Rhino exit, War Miner, Radar, Tesla Coil, low power, minimap NOT tested.
2. **MAJOR - Chosen colour not used.** Set green. Units and buildings stay Soviet red (s09, s10, s23). Enemy units are blue. STILL BROKEN (round-1 #9).
3. **MAJOR - Combat still invisible**: no shots, flashes, explosions, wrecks (only a tiny scorch, y4), no "unit lost / building lost" EVA. STILL BROKEN (#4).
4. **MINOR - No queue count on cameo.** Clicking Conscript 4 times builds 4 (cost charged each, s23->u2->u3), but no number shows on the cameo. Queue itself FIXED, count display missing.
5. **MINOR - Miner drives far away** (y2: goes to lower-left edge though ore is next to the base). STILL BROKEN (#17).
6. **MINOR - Arrow scroll still too fast**: hold Left 1.5 s moved the view about 1.5 screens (y5). STILL BROKEN (#15).
7. **MINOR - Tanks stack/overlap** in one spot (s19 zoom). STILL BROKEN (#5, partly).
8. **MINOR - Status line cut off**: "BUILDING SOVIET ORE REFINERY." wraps ok, but "CONSTRUCTION YARD DEPLOYED. CHOOSE A BUILDIN" cut (s10). Stale text "MINERS HARVEST AUTOMATICALLY" stays (u1). STILL BROKEN (#11).
9. **POLISH - Esc Abort needs two steps** (pause, then "Abort match?" confirm; Enter works). Fine, like original options flow; the Abort click goes to a second dialog first.
10. **POLISH - Sidebar is plain**: no tab icons, no power bar graphic, "DEPLOY YOUR MCV FIRST" text remains when yard is gone (s16).
11. **POLISH - Help text says "F1: SHOW HELP"** while the panel is open (v1); should say close.

## Round-1 retest
- #1 Production time: Reactor ~7 s, Barracks ~4 s, Sentry ~6 s, Refinery ~18-20 s, Conscript ~1 s each (frame counter in log counts render frames about 100/s, so I report seconds). Even and plausible: FIXED (Conscript maybe too fast; not compared with original).
- #2 Factory exit: Conscripts walk out of Barracks and stand below it (u2, u3): FIXED. Rhino from War Factory: NOT TESTED.
- #3 Build queue: works (4 Conscripts). Count on cameo missing: PARTLY FIXED.
- #4 Combat invisible: STILL BROKEN.
- #5 Movement slow: tanks moved 300 px in about 3 s: speed looks FIXED; stacking STILL BROKEN.
- #6 War Miner cameo: NOT TESTED (no War Factory).
- #7 Low power: NOT TESTED.
- #8 Cyan blobs: none seen in the area I played: FIXED (limited view).
- #9 Setup: Game Speed and Unit Count sliders work and are visible (6 max, 3 gave 3 Rhinos + MCV): FIXED. Colour on units: STILL BROKEN. Start position "???" still shown: STILL BROKEN. No map name/preview: STILL BROKEN.
- #10 Esc: pause menu with Resume/Abort works; Resume restores play: FIXED.
- #11 Status line: STILL BROKEN.
- #12 F1: toggles help mid-match (v1): FIXED.
- #13 Guidance: "SELECTED UNITS" list now has title and shows "RHINO HEAVY TANK 335" (x1): FIXED. Power now shows "POWER OUT/USE" with a bar: FIXED (power text readable).
- #14 Placement footprint, #16 health bars, #18 Easy AI, #19 tabs/cost: cost chip on hover FIXED ($600 etc. shown, s11); rest NOT TESTED.

## Worked well
Setup sliders and dropdowns, game start with help panel, deploy MCV, building cameos with clock wipe and READY, placement by click, refund on cancel, costs drained while building, Esc pause/abort flow, F1 toggle, selected-unit list, no crashes.
