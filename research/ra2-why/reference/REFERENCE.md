# RA2 reference numbers (original game.exe under Wine)

Game: Red Alert 2 base game, `game.exe` version 1.006TUC, cnc-ddraw, Wine 11.17, hidden Xvfb 1024x768x24, no sound.
Mode: Skirmish, 1 human vs 1 computer, map "Roundhouse Kick (2-4)" (default), Credits 10000, Unit Count 10,
MCV Repacks on, Crates on, Short Game off, Superweapons off.
**Game Speed: 3** on the skirmish slider (the middle of 0..6; the game saves it as `GameSpeed=3` in RA2.INI).

Frame rate at Game Speed 3: **20.7 frames per second** (measured with `bin/fpscount` while units move: 20.73–20.75 screen
changes per second in 4 runs). Frames below = seconds x 20.75, rounded.

How times are taken: `bin/buildtime` clicks the sidebar icon and watches it until the clock ends ("Ready" for buildings,
the icon goes back to normal for units). Time = click to end. The start includes about 0.05 s input delay.
Power was never low during the tests (one Power Plant).

## Build times (Allies / America, two separate matches)

| Item | Cost | Run 1 (s) | Run 2 (s) | Frames (approx.) |
|---|---|---|---|---|
| Power Plant | 800 | 23.37 | 23.42 | 485 |
| Barracks | 500 | 13.04 | 13.04 | 271 |
| Ore Refinery | 2000 | 59.71 | 59.81 | 1240 |
| War Factory | 2000 | 59.70 | 59.74 | 1239 |
| Airforce Command | 1000 | — | 29.73 | 617 |
| GI | 200 | 5.25 (and 5.3 in a frame-by-frame watch) | 5.25 | 109 |
| Grizzly Tank | 700 | 20.83 | 20.84 | 432 |

Soviet (Russia), one match, power OK unless noted:

| Item | Cost | Time (s) | Frames (approx.) |
|---|---|---|---|
| Tesla Reactor | 600 | 15.65 | 325 |
| Barracks | 500 | 13.03 | 270 |
| Ore Refinery | 2000 | 59.68 | 1238 |
| Radar | 1000 | 28.64 | 594 |
| War Factory (power was LOW during the build) | 2000 | 61.76 | 1282 |
| Sentry Gun | 500 | 13.04 | 271 |

Allied defenses: Pillbox 13.07 s (271 frames). Airforce Command 29.73 s (above). Soviet Radar = "Radar Tower": 28.64 s.
Not measured: War Miner, Rhino build times (the War Factory game was overrun by the AI).

Rule of thumb seen here: about 2.95–3.0 s per 100 credits for Power Plant, Refinery, War Factory, Airforce Command and
Grizzly; Barracks and GI are faster (about 2.6 s per 100 credits).

## Unit speed (clear snow ground, straight path along one map axis)

Method: select one unit, `bin/track` follows its green health bar every ~45 ms, then a straight-line fit of the steady part.
1 cell along a map axis = 30 px right + 15 px down on the screen (33.5 px).

| Unit | Path | Run 1 | Run 2 (way back) | Per frame |
|---|---|---|---|---|
| Grizzly Tank | 11 cells | 2.173 cells/s (11 cells in 5.06 s incl. start/stop) | 2.167 cells/s | 0.105 cells = ~27 leptons |
| Rhino Tank | 7 cells | 1.924 cells/s (7 cells in 3.70 s incl. start/stop) | 1.914 cells/s | 0.093 cells = ~24 leptons |

| GI | 7 cells | 0.80 cells/s (7 cells in 8.9 s incl. start/stop) | — | 0.039 cells |
| Conscript | ≈ 5 cells, bent path | 0.72–0.78 cells/s (noisy) | — | ≈ 0.036 cells |

Grizzly is about 13% faster than Rhino. Raw logs: `runs/track-gi-1.txt`, `runs/track-conscript-1.txt`.
Not measured: War Miner speed, and any speed on road (the default map "Roundhouse Kick" has no road tiles). Before a unit moves back the way it came, it turns for about 1.2–1.7 s.
Raw logs: `shots/track-grizzly-1.txt`, `shots/track-grizzly-2.txt`, `shots/track-rhino-1.txt`, `shots/track-rhino-2.txt`
(columns: seconds, health-bar x, health-bar y).

## Queue limit

- One sidebar icon counts up to **30** and then ignores more clicks (GI clicked 65 times: the icon shows 30).
- The limit is for the whole infantry queue: with 29 GIs queued, the Engineer icon took only 1 (29 + 1 = 30).
- Right click on the icon removes one from the queue; when all are removed the icon first shows "On Hold".

## Weapon tests (own units, forced fire with Ctrl, Game Speed 3)

The game does not let one player own Allied and Soviet units at the same time. So each weapon was tested on my own unit
of the same side. The target's health bar was read every ~40 ms (`bin/hbar`, `bin/fight`). t = 0 is the Ctrl+click order.
Vehicle bars are 17 px long, infantry bars 8 px. Raw logs: `runs/fight-*.txt` (seconds, bar px, bar color g/y/r).

| Attacker -> target | Hits to kill | Time between hits | Kill time after order | Damage per hit |
|---|---|---|---|---|
| Grizzly -> Grizzly (in range) | 5 | 2.95 s (≈ 61 frames) | 13.0 s (first hit 1.3 s) | 4 of 17 bar px (≈ 23%) |
| Grizzly -> Grizzly (drives into range first) | 5 | 2.91 s (≈ 60 frames) | 16.0 s (first hit 4.4 s) | 4 of 17 px |
| Rhino -> Rhino | 5 | 3.19 s (≈ 66 frames) | 14.1 s (first hit 1.3 s) | 4 of 17 px (≈ 23%) |
| GI -> GI | about 12 | ≈ 1.06 s (≈ 22 frames) | 13.35 s | ≈ 1 of 8 px per hit; target lies down (prone) |
| Conscript -> Conscript (side by side) | about 7–8 visible steps | ≈ 3.1 s per bar step | ≈ 22 s | 1 of 8 px per step; target prone |
| Pillbox -> GI | 4 bursts | ≈ 1.2 s (≈ 25 frames) | ≈ 4.4 s (≈ 91 frames) | GI falls at 4.3–4.5 s (screenshots `runs/pillbox-gi2/`) |

Estimates for the asked cross-side pairs (NOT measured directly; uses the rules.ini hit points Grizzly 300, Rhino 400
and the measured hits/reload above):
- Grizzly -> Rhino: ≈ 65–70 damage per hit -> 6–7 hits -> ≈ 15–18 s after the first hit.
- Rhino -> Grizzly: ≈ 90 damage per hit -> 4 hits -> ≈ 9.6 s after the first hit.
- Pillbox -> Conscript: like Pillbox -> GI, ≈ 4 s.
- Sentry Gun -> GI: not measured cleanly. `runs/sentry-vs-gi/` has a 20 s clip of a new Sentry Gun in a mixed fight with
  dropped enemy paratroopers (not a 1-vs-1 test).

## 3 Rhinos vs Construction Yard

Setup: Soviet (Russia) match, 3 starting Rhinos (no veterancy) at about 3–5 cells from my own fresh Soviet Construction
Yard (full health), force-fire order (Ctrl + click) at t = 0. Screenshots every ~0.35 s in `runs/rhino-cy3/`.

- First shot (muzzle flash): t ≈ 0.7 s.
- Yard still standing (burning) at t = 19.3 s; gone (big explosion) at t = 19.7 s.
- **Time to destroy: about 19.5 s from the order (≈ 405 frames), about 18.8 s of firing (≈ 390 frames).**
- When it dies, infantry survivors run out of it.

## Starting units (Unit Count 10, default)

- Allies (America): 1 MCV + 5 Grizzly Tanks + GIs. The GI count changed between matches: 7, 5, 5, 5 (4 matches).
- Soviets (Russia): 1 MCV + 4 Rhino Tanks + 12 Conscripts (same in 2 matches).
- Credits at start: 10000 (the counter counts up at match start).

## Sidebar (Allies)

- After the MCV deploys: only Power Plant (row 1, left).
- After Power Plant: Power Plant, Ore Refinery (row 1); Barracks (row 2, left).
- After Ore Refinery + Barracks: row 1 Power Plant | Ore Refinery; row 2 Barracks | War Factory;
  **row 3 Airforce Command | Naval Shipyard**.
- After War Factory: row 4 Service Depot (left) | Battle Lab (right, appears after Airforce Command is built).
- So the Allied radar building (Airforce Command, "Air Force Command Headquarters" in the tooltip) is in **row 3, left column**,
  and it appears when Ore Refinery and Barracks exist (it was there before the War Factory was built).
- Sidebar tabs (left to right): buildings, defenses, infantry, vehicles. Infantry tab: GI, Engineer, Attack Dog.
  Vehicle tab (with War Factory and refinery): Chrono Miner, Grizzly Tank, IFV, Nighthawk Transport.
- Soviet sidebar after deploy: Tesla Reactor (row 1, left).

## AI first attack (human = Allies/America, AI color orange, Game Speed 3)

Time is from the moment the battle view first shows (after the ~5 s load). Frames = s x 20.75.

| AI | My base | First enemy fire at my base | Notes |
|---|---|---|---|
| Easy | MCV deployed + Power Plant + Ore Refinery + Barracks, army stays home | **529–534 s (8.8–8.9 min, ≈ 11 000 frames)** | 2 orange units come in from the east and shoot; my tanks kill them. `runs/ai-easy-opening/t0529.jpg`, `t0534.jpg` |
| Easy | full base (PP, Barracks, Refinery, War Factory, Airforce Command), army sent away to scout | between 7.2 and 8.1 min | 3 Rhinos killed Barracks + Airforce Command and shot the Construction Yard (`shots/08-fight-ai-attack.png`) |
| Medium | MCV deployed + Power Plant (the refinery stayed unplaced: my script got stuck) , army home | **794–799 s (13.2–13.3 min, ≈ 16 500 frames)** | orange infantry + a vehicle come in from the west; the Power Plant is destroyed by 804 s. `runs/ai-medium-opening/t0794.jpg`..`t0809.jpg` |
| Easy | only the deployed Construction Yard, nothing else built | **no attack in 37.9 min** | `runs/ai-easy/` (screenshot every 10 s) |
| Medium | only the deployed Construction Yard, nothing else built | **no attack in 37.9 min** | `runs/ai-medium/` |

| Brutal ("Hard"; RA2 has Easy / Medium / Brutal only) | MCV + Power Plant + Ore Refinery + Barracks, army home | **324–330 s (5.4–5.5 min, ≈ 6 800 frames)** | a group of orange tanks comes from the south; by 416 s the base is mostly destroyed. `shots/09-brutal-ai-first-attack.jpg`, `runs/ai-brutal-opening/` |
| Easy | Soviet base (2 Tesla Reactors, Barracks, Refinery, Radar, War Factory), army home | about 6 min (first shot seen on the War Factory at ≈ 6 min) | not watched every 5 s; base nearly destroyed by ≈ 7.5 min |

So the AI does not attack a player who builds nothing. With a normal opening, Easy attacks first at about 8–9 min and
Medium at about 13 min, Brutal at about 5.5 min (one sample each; AI timing varies between games).

## Screenshots (shots/)

| File | What |
|---|---|
| `01-main-menu.png` | main menu (Version 1.006TUC) |
| `02-skirmish-setup.png` | skirmish setup: America vs Easy, Game Speed 3, Credits 10000, Unit Count 10 |
| `03-match-start.png` | first seconds of an Allied match (MCV, 5 Grizzlies, 7 GIs) |
| `03b-match-start-soviet.png` | first seconds of a Soviet match (MCV, 4 Rhinos, 12 Conscripts) |
| `04-sidebar-after-deploy.png` | Construction Yard deployed; sidebar shows Power Plant only |
| `05-sidebar-airforce-command.png` | sidebar with Airforce Command (row 3 left) and Naval Shipyard |
| `06b-fight-first-shot.jpg` | 3 Rhinos fire the first shots at the Construction Yard |
| `06-fight-rhinos-firing.jpg` | yard burning under fire (t = 18 s) |
| `07-fight-cy-explodes.jpg` | yard explodes (t = 20 s) |
| `08-fight-ai-attack.png` | Easy AI attack: 3 orange Rhinos shell my burning Construction Yard |
