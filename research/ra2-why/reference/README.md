# RA2 reference oracle

This folder runs the ORIGINAL Red Alert 2 (`game.exe`, version 1.006TUC, base game, not Yuri's Revenge)
under Wine 11.17 on a hidden Xvfb screen. Agents drive it with fake mouse and keys and take screenshots.
Use it to compare the native Linux rebuild against the real game. The measured numbers are in `REFERENCE.md`.

## Commands

```
./oracle start N            start hidden screen :N (1024x768, 24-bit) and game.exe. The main menu shows after about 20 s.
./oracle shot N FILE.png    screenshot of screen :N (ImageMagick import). Open FILE.png with the Read tool.
./oracle in N <args>        fake input on :N (bin/qa-input):
                              click left X Y | click right X Y | dclick X Y | drag X1 Y1 X2 Y2 | move X Y
                              key NAME | keydown NAME | keyup NAME | hold NAME MS   (X keysym names)
./oracle wait MS            wait real time
./oracle alive N            "running" or "NOT RUNNING"
./oracle log N              last lines of the Wine output
./oracle stop N             stop the game, its Wine server and the screen
```

Use a display number that no other agent uses (for example 91..99).
Each display N gets its own copy in `instances/N/` (Wine prefix + game folder of links), so several games can run at the same time.
The copy is made on the first `start N` and kept. Delete `instances/N` to reset it.

## Helper tools (bin/)

- `bin/buildtime N X Y` click the sidebar icon at X,Y and print the seconds until it is ready (watches the icon).
- `bin/place N X Y` click a "Ready" building icon at X,Y and put the building on the first free spot near the base.
- `bin/track MS` (with `DISPLAY=:N`) print the position of the health bar of the ONE selected unit every ~45 ms.
- `bin/speedfit FILE` turn a `bin/track` log into cells per second (1 map cell = 30 px right + 15 px down on screen).
- `bin/fpscount X Y W H MS` (with `DISPLAY=:N`) count how often a screen area changes. With units moving, this is the frame rate.
- `bin/aiwatch N DIR MIN` press Start Game on the skirmish screen, deploy the MCV, then save a screenshot every 10 s.
- `bin/shotloop N DIR T0 MIN` keep saving screenshots every 10 s.
- `bin/opening N DIR T0 MCVX MCVY MIN` deploy the MCV, build Power Plant, Ore Refinery, Barracks (Allies), then save a screenshot every 5 s and log enemy-orange pixels (set the AI color to orange). A count above 50 means enemy units are in view.

## Screen layout (1024x768)

- Main menu: Single Player (830,304). Single Player menu: Skirmish (830,388).
- Skirmish screen: Start Game (830,346). AI player 1 difficulty list (385,200): None (260,225), Easy (280,248), Medium (287,271).
  Human side list (569,174): America (490,222); scroll the list down 3 times (569,337) to reach Russia (490,337).
  AI color list (632,200): orange (615,317). Game Speed slider: middle value 3 at (553,389).
- The game saves the last skirmish settings in `instances/N/game/RA2.INI`, so the next start keeps them.
- In a match: battle view x 0..850, sidebar at the right. Sidebar tabs: (893,210) buildings, (922,210) defenses,
  (953,210) infantry, (984,210) vehicles. Icons: column x 907 and 969, rows y 252, 300, 350, 400 ...
- Left click selects and orders. Right click clears the selection. Click a selected MCV again to deploy it.
- Force fire: `keydown Control_L`, wait 0.3 s, click the target, wait 0.3 s, `keyup Control_L`.
  If you release Ctrl too fast, the game does not see it (the game reads keys once per frame).
- Shift + click adds a unit to the selection (hold Shift the same way). `key h` jumps to the base.

## How it is set up

- `game/` holds links to the owner's install (not changed). These files are real copies because the game writes them:
  `*.INI`, `ddraw.ini`, `*.exe`, `*.dll`, logs, `RMCache/`.
- `ddraw.dll` in the install is cnc-ddraw. `game/ddraw.ini` [ddraw] is changed to: windowed=true, fullscreen=true
  (borderless), border=false, renderer=gdi, posX=0, posY=0, savesettings=0. Xvfb has no GPU, so GDI is used.
- Wine loads the game's own ddraw.dll with `WINEDLLOVERRIDES="ddraw=n,b"`.
- Sound is off: the prefix has `HKCU\Software\Wine\Drivers Audio=""` (no sound device). The game runs silent.
- No CD check and no launcher: `game.exe` (TUC 1.006) starts straight to the main menu. No registry keys are needed.
- Xvfb comes from the Arch package xorg-server-xvfb 21.1.24-1, unpacked in `dl/xvfb` (no sudo).
- Game speed: Game Speed 3 (middle of 0..6) runs at about 20.7 frames per second here (measured).

## Notes

- The default skirmish map (Roundhouse Kick, snow) looks pink/purple. Menu, sidebar and unit colors look correct, so it is most likely the map's lighting, not a color bug. Not checked on other maps yet.
- `oracle stop N` kills all Wine programs of that instance only.
