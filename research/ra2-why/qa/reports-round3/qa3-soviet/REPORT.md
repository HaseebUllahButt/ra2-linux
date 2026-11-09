# qa3-soviet report (display 131), build 00065f43
Setup: Russia vs Medium AI, Unit Count 10 (default), colour PURPLE, Game Speed 3 (slider middle value; screen shows it after my click at the slider), start credits about $10000. About 25 min real play. One match. NO VICTORY reached (my one big attack failed; see below). No crash, no freeze (./qa alive = running at the end). Debug log: debug-loop.log.

## Steps
1. Menu > Skirmish (m0-m6). Set Medium, purple, speed 3. Start (g1).
2. Deploy MCV (D), Reactor, Ore Refinery, Barracks, Radar, War Factory, Sentry Gun, 2nd Reactor, Flak Cannon cameo, War Miner (g3-g47).
3. Queued 5 Conscripts and 5 Rhinos (g16, g35). Built about 13 Rhinos and 5 Conscripts.
4. Sent 13 Rhinos to the enemy base in the far west (g46-g53). They fought, then all died (g54). Rebuilt 5 more Rhinos (g55-g57). Time ran out before a second push.

## Round-3 results (colour, round-2 blocker)
- Colour: units, buildings and minimap dots are all PURPLE (g22, mm1). Enemy is pink. FIXED (round-2 #2).
- Round-2 BLOCKER (Construction Yard dies at 70-90 s): in 25 min my yard was never destroyed. FIXED in my match. But I saw no enemy attack at my base at all, so idle defence auto-fire (Sentry Gun, idle Rhinos firing at attackers) is NOT TESTED. Tesla Coil: NOT TESTED (not built; needs Battle Lab etc).
- Power: after the first Reactor was lost/burning (g25: explosion, "POWER OUT 0 USE 85", red text) the sidebar dropped several cameos (Barracks, Ore Refinery) and the bar went red (g24). I did not see who hit the reactor (small group of units near it at left, g23-g25). Rebuilding a Reactor restored everything (g33). Low power effect is visible: PARTLY FIXED. Radar minimap kept working in my low-power moment? Not clearly checked.
- Queue: cameo shows a count (5 on Rhino, g35; "1" on others). Count display FIXED. Costs charged per unit.
- War Factory exit: Rhinos leave and gather at the right/below of the factory (g43). Several overlap in a tight diamond pile (z2.png) but separate when moved. Exit FIXED; stacking minor.
- War Miner cameo exists and a second miner was built (g29-g41). FIXED.
- Radar Tower built (g34-g36); minimap appears after Radar (g37). Minimap shows my purple dots and enemy pink dots (mm1.png). FIXED.
- Production times plausible, even. Reactor/Barracks about 8-10 s, Refinery about 20 s, Rhino about 10-15 s.

## Bugs seen
1. MAJOR - Combat in the enemy base: 13 Rhinos attacked and all died; I could not see what killed them (g52-g54). Hits and explosions are now partly visible (fire, g25), but no clear shots or enemy shooters on screen; unit loss has no EVA message. Rhino hp fell 400 -> 190 -> 1 in about 25 s with no visible attacker (g49, g50). Likely off-screen fight; combat feedback is still weak. (round-1 #4: PARTLY FIXED)
2. MAJOR - Cannot confirm victory/score screen: not reached.
3. MINOR - Sidebar page layout changes when tech changes (Barracks/Refinery cameos disappear when built or power lost, rows shift). I clicked the Flak Cannon by mistake while queueing Rhinos (g54: "PLACE FLAK CANNON"). Original keeps cameo positions stable per tab.
4. MINOR - Rhinos stack in a tight pile at the factory exit (z2.png).
5. MINOR - Status line stale text "MOVING." and "CANNOT DEPLOY HERE" stays (g26, g31). Text "QUEUED RHINO HEAVY TANK." fine.
6. POLISH - Sentry Gun needs placement near yard; first placement attempts said "CANNOT DEPLOY HERE" with no reason shown (g31, g32); the message should be "Can't build here".
7. POLISH - Enemy buildings in the pink base show a thick black outline on pink art (g46); fine but darker than the original.

## Round-2 items
- Yard dies at 70-90 s: FIXED (not seen).
- Colour not used: FIXED.
- Queue count on cameo: FIXED.
- Combat invisible: PARTLY FIXED (fire and smoke seen, no shots seen).
- Stack/overlap: STILL BROKEN (minor).
- Miner drives far: NOT TESTED well; one miner went to the far west edge near ore (g41, g48), that looks far from the base: STILL BROKEN.
- Status text stale: STILL BROKEN.
- Arrow scroll speed, F1 text, Esc pause: NOT TESTED.

## Worked well
Setup screen sliders, colour choice, MCV deploy, build placement, ready banners on cameos, queue count, War Miner, Radar + minimap, radar dots in the player colour, steady economy ($10000 start, income kept me above $2000), stable game (no crash).

## Not done
Full victory; Sentry/Tesla auto-fire test under attack; Esc pause; F1 re-check; low-power effect on radar.
