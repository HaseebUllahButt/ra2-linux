# QA report: qa-economy (display 102) - Soviet economy, power, sidebar
Build 87292f36. Played about 35 minutes. Game never crashed or froze (`./qa alive` = running at the end).

## What I did
1. Main menu > Single Player > Skirmish. Russia and Easy Enemy were already set. Start Game (s01-s04).
2. Selected the MCV, pressed D. Construction Yard deployed (s06).
3. Built Tesla Reactor (cost $600, ~9 s), Barracks ($500, ~10 s), Ore Refinery ($2000), Radar, Sentry Gun ($500, ~6 s), War Factory, Rhino Tank, second Ore Refinery.
4. Lost the reactor to an enemy raid, so I got real low power (0/110, then 0/135). I rebuilt it (s32-s38).
5. Built a second refinery while power was short (150/185, red bar) (s41).
6. Watched the free War Miner harvest and return (s24-s29).

## Bugs

### 1. MAJOR - First Ore Refinery takes about 7 minutes to build
- Steps: Skirmish, Russia. Build Tesla Reactor, Barracks. Click Ore Refinery cameo ($2000).
- Expected: roughly 1 min or less. The reactor took 9 s and the barracks 10 s.
- Got: credits dropped only 4-18 $/s. Ready after about 7 min (s13-s22). The second refinery later took about 45 s (s40). Radar took about 2 min. Build time is very uneven.
- Screens: s16, s19, s22, s40.

### 2. MAJOR - Cannot build a second War Miner
- Steps: have War Factory and two refineries. Open the VEH tab or ALL tab.
- Expected: War Miner cameo ($1400 in the original) next to the Rhino Tank.
- Got: only Rhino Tank is listed. The only miners were the free ones (one per refinery).
- Screens: s42, s44.

### 3. MAJOR - Rhino tanks built in the War Factory do not drive out
- Steps: with War Factory placed, build Rhino Tank. Wait.
- Expected: the tank leaves the factory.
- Got: the tank sprite sits on top of the factory roof. The second Rhino stacked there too (s40, s44).

### 4. MAJOR - Low power has no visible effect on the radar
- Steps: lose the reactor (power 0/110) while the Radar Tower is built.
- Expected: minimap goes dark / radar offline.
- Got: minimap kept showing the map the whole time. Build speed under low power also looked unchanged (Rhino ~29 $/s in s34-s35).
- Screens: s33, s35. Note: the power bar was empty with no red when produced was 0 (s34). It turned red at 150/185 (s41).

### 5. MINOR - Miners walk very far from the refineries
- Both refineries sit beside ore. The miners drove 300+ px off-screen to the west/north (s25-s27, s39, s43). One trip took over 90 s. The original picks the closest ore.

### 6. MINOR - Credits jump in large lumps
- Credits went from $5900 to $7846 (s27 to s28) while the miner was still far from the refinery. They went $8730 to $9400 to $11400 (s31-s33). The miner never visibly docked between shots, so I could not prove the cause. Real money income may be correct, but the timing looks wrong. Please check the unload logic.

### 7. POLISH - Status line text is cut off and stays wrong
- "BUILDING SOVIET ORE REFINE" is cut off (s13). It still says "BUILDING ..." when the item is READY (s09, s27, s32).
- The sidebar cameo has no cost or time label. No on-hold / disabled look was seen.

### 8. POLISH - Sidebar is not the original look
- It is a plain flat sidebar with a single ALL tab list plus BLD/INF/VEH/AIR. There is no Repair / Sell / Power-bar art.
- Skirmish setup screen shows Game Speed / Unit Count with no slider.

### 9. NOTE (unclear) - 10 extra Rhino-like tanks at start
- At start I had 10 tanks next to the MCV. In the original I would get only an MCV. Not sure if this is by design.
- Free War Miner appeared only when the first refinery was placed (s24). I did not see it start before that.

## Works well
- Menu flow, Russia on Easy, and MCV deploy with D work.
- Credits drop while an item builds. Total costs match: Reactor 600, Barracks 500, Sentry 500.
- Power numbers are right: reactor +150, barracks 10, refinery 50, radar 50, War Factory 25. Display "made/used".
- Cameo "READY" text and the pie clock sweep appear. Placing a building by clicking the map works.
- Barracks and Refinery cameos disappear when the reactor dies and come back on rebuild (matches the power-plant tech rule).
- Free War Miner harvests and returns. Credits rise. The second refinery also gave a free miner.
- Parallel build of a structure and a vehicle works.

## Screenshots
s06 deploy, s09 reactor ready, s13 barracks and refinery building, s19 clock, s22 refinery ready, s25-s27 miner, s28-s33 radar, low power, s38 reactor rebuilt, s41 red power bar, s42 no miner cameo, s44 stuck Rhinos. Game log tail: game-log.txt.
