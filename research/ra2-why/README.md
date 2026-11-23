# Red Alert 2 — native Linux + Android

Date: 2026-11-07. Machine: REFERENCE_SYSTEM (ARM64). Source: your install on the test-host.

## What was pulled

- Full Red Alert 2 + Yuri's Revenge install: 1.9 GB, 498 files.
- Now at `/path/to/user/ra2/game` (game.exe, gamemd.exe, ra2.mix, language.mix, ra2md.mix, langmd.mix, thememd.mix, maps, videos).
- Note: base-game music IS present as `THEME.MIX` (uppercase name; 16 tracks, all matching the mod music list). Yuri Revenge music is in `thememd.mix` (10 WAV tracks).

## REA findings (evidence from REA 6.7.0 + Ghidra 12.1.4, run locally)

Both `game.exe` and `gamemd.exe` are 32-bit Windows PE files, image base 0x400000.

| Fact | game.exe | gamemd.exe |
|---|---|---|
| Procedures found | 8,441 | 8,951 |
| Strings found | 7,722 | 8,376 |

Shared DLL dependencies (both binaries):

- Graphics: `DDRAW.dll` (2D blitting, 800x600 surface)
- Audio: `DSOUND.dll`, `WINMM.dll`
- Video cutscenes: `binkw32.dll`
- Network: `WSOCK32.dll`
- System: KERNEL32, USER32, GDI32, ADVAPI32, SHELL32, ole32, OLEAUT32, VERSION, COMCTL32, IMM32

Asset archives: `.mix` files use the Westwood MIX container
(first bytes `00 00 03 00` = plain header, 3 nested archives inside `ra2.mix`).
OpenRA reads this format natively.

## Community reverse-engineering work you can build on

The RE of this engine was already done by the community, in the open:

- **OpenRA RA2 mod** (github.com/OpenRA/ra2) — the game's rules, units, sounds
  and maps rebuilt as open data on a clean native engine. Used here.
- **CnCNet + YR client** — open-source client and multiplayer for RA2/YR.
- **Ares / YRpp** — open-source engine extensions that document internal
  structures of the original binary (class layouts, hooks).
- **XCC format docs** — public file-format documentation for MIX, SHP, VPL, VXL,
  and the INI rule system.

Coverage today: Allied and Soviet sides play; Yuri's Revenge side content
(new units, Yuri faction) is incomplete in the mod. That is the community
frontier — contributing there moves "the real RA2" forward legally.

## What this means

The engine is a 32-bit DirectDraw game. Three legal paths follow from the evidence:

1. **Native Linux (done here):** OpenRA is a clean-room, GPL engine — no EA code.
   The community RA2 mod for OpenRA loads your own `.mix` files.
   Built and verified on this REFERENCE_SYSTEM (ARM64); same steps work on x86 Linux.
2. **Android:** run your own game files in a Wine runner (Winlator or Mobox).
   The folder already contains `cnc-ddraw` (`ddraw.dll` + `ddraw.ini`), which is
   exactly what those runners need for DirectDraw games.
3. **What we did not do:** rebuild EA's engine code from the decompilation.
   REA was used for interoperability analysis (formats, dependencies, structure) —
   not to recreate the game's program. That line stays.

## Native Linux — what is running on this REFERENCE_SYSTEM

- `/path/to/user/ra2/openra/ra2` — OpenRA engine `release-20231010` + RA2 mod, built from source.
- Content links in `~/.openra/Content/ra2/` point at your game files.
- Dedicated server verified: `./launch-dedicated.sh Mod=ra2` starts and joins the master server.
- Content check verified: `./utility.sh --check-yaml` loaded your originals and
  tested all 28 official maps with 0 errors (The Alamo, Tsunami, Snow Valley...).
- Play with a screen: `./launch-game.sh` (needs the content links, already set).

## Android — how to install

See `android/README.txt`. Short version:
1. Install Winlator (or Mobox) on the phone.
2. Copy `ra2-android-pack.zip` contents to the device storage.
3. Add the `game` folder as a container drive, run `RA2.bat` (Yuri's Revenge) or `RA2.bat --ra2` (base game).

## Files here

- `game/` — your full install (1.9 GB)
- `analysis/` — REA Evidence JSON for game.exe and gamemd.exe
- `openra/ra2/` — built OpenRA + RA2 mod
- `scripts/setup-native-linux.sh` — repeat one-machine setup
- `android/` — Android pack instructions + launcher script

---

## Session 2 (2026-11-08): deep RE — formats, text, maps, landmarks

Continued reverse engineering for preservation. Full report:
`analysis/RE-REPORT-2.md`.

- All 9 MIX archives parse (parser: `scripts/mixlib.py`); 297 of 455
  entries resolved to real names. Corrected session-1 note: the MIX
  headers are Blowfish-encrypted (flags=3), not "3 nested archives".
- Found the game's own `[PublicKey]` INI inside the archives — the
  archive crypto key material ships with the game.
- Recovered all game text: 5,211 + 4,479 strings decoded from the
  CSF tables (XOR-0xFF UTF-16 variant documented from evidence).
- Extracted 175 map files as plain INI text (multiplayer map names
  from the official spawn list) plus rules, sound and spawn configs.
- Ghidra headless dump of gamemd.exe: 8,584 functions indexed, 139
  landmark functions decompiled (`analysis/ghidra-dump/`), engine
  landmarks table in the report. Rules class pointer DAT_00A8B230
  matches community documentation for YR 1.001.

Analysis-only work; no EA program code rebuilt or redistributed.

---

## Verified end to end on Linux (2026-11-08)

The game was driven with real input under a virtual display. All steps
ran on this ARM64 REFERENCE_SYSTEM, native Linux, no Wine:

1. First-run screens -> main menu (Allied + Soviet logos render).
2. Skirmish lobby -> original Westwood map "Snowball's Chance" ->
   live match with an AI opponent; game timer advanced.
3. Dedicated server on port 1234 -> client auto-joined with
   Launch.Connect -> live multiplayer match on "Pinch Point".

Fixes that made it work:
- Content links belong in ~/.config/openra/Content/ra2 (modern Linux
  support dir), not only ~/.openra.
- A broken theme.mix link fails the content check; links now point at
  the real THEME.MIX (16 tracks, full base soundtrack).

Evidence screenshots: analysis/verification/.
Setup script updated: scripts/setup-native-linux.sh.
