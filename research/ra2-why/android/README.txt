Red Alert 2 on Android — Winlator / Mobox pack
==============================================

What is in this pack
- game/          Your full Red Alert 2 + Yuri's Revenge install (1.9 GB)
- RA2.bat        Launcher: Yuri's Revenge (default) or base game with --ra2
- README.txt     This file

The game folder already contains cnc-ddraw (ddraw.dll + ddraw.ini).
That is the piece that makes a DirectDraw game work on modern devices.

Steps (Winlator, recommended)
1. Install Winlator from winlator.org on the Android device.
2. Unzip this pack to device storage, e.g. Download/ra2-pack/
3. In Winlator: create a container, then add the game folder as a drive.
   Container -> Drives -> add D: -> Download/ra2-pack/game
4. Open the container's file browser, go to D:, run RA2.bat
5. In cnc-ddraw settings (ddraw.ini) if needed:
   width=800, height=600, renderer=gdi, singlecpu=true

Steps (Mobox)
1. Install Mobox, add the game folder to the drive list.
2. Run RA2.bat the same way.

Controls
- Touch acts as the mouse. Long-press = right click (queue orders).
- Winlator: turn on "DirectInput" gamepad mapping for keyboard shortcuts.

Notes
- Yuri's Revenge = gamemd.exe. Base Red Alert 2 = game.exe (needs RA2 disc
  content your install already has).
- Multiplayer: CnCNet works on PC; phone play is single player and LAN.
- Keep your game files private; do not redistribute them.
