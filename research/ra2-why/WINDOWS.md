# Windows path

Red Alert 2 and Yuri's Revenge are Windows games. Your install already
runs on Windows as-is:

1. Original play: run game.exe (RA2) or gamemd.exe (Yuri's Revenge)
   from game/ on any Windows PC. No key needed for the files you own.
2. Modern Windows (wide screen, no crashes): use cnc-ddraw. The pack
   in android/ra2-android-pack.zip ships ddraw.dll + ddraw.ini and
   RA2.bat. Unpack next to the game folder and run RA2.bat.
   The same pack works for Android via Winlator or Mobox.
3. The rebuild (rebuild/) targets Windows too: phase 3 is a hook DLL
   that loads next to gamemd.exe; phase 4 is a full replacement exe.

## Linux

See README.md "Verified end to end on Linux". The OpenRA + community
RA2 mod path is fully native and verified on this machine.

## Android

Separate project. The pack and instructions are in android/.
