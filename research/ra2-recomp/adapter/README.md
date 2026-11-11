# RA2 Windows parts

The adapter contains a portable surface core and a Windows DirectDraw DLL.
J10 is incomplete. No skirmish or 20-frame equality result exists yet.
J11, J12, and J13 have design notes only.

Read [DESIGN.md](DESIGN.md) for scope, ABI evidence, reference licences, and acceptance checks.

Build and test the Linux core:

```
make all test test-tools
```

Build and test the Windows DLL under Wine:

```
./tools/fetch_sdl_windows.sh
./tests/run_windows.sh
```

The compiler defaults to `../tools/wcc`. Override `WCC` for another Windows x86 compiler.
The Windows SDL2 path defaults to `.reference/SDL2-*/i686-w64-mingw32`. Override `SDL_WIN` when needed.
The DLL links the SDL2 import library. Put `SDL2.dll` beside `ddraw.dll`.
Use `WINEDLLOVERRIDES=ddraw=n` to select this DLL under Wine.
The test loads the local DLL explicitly and checks that each call succeeds.
All Wine tests use `tools/hidden_display.sh`. They use Xvfb and disable audio output.
Install Xvfb or set `XVFB_BIN`. The local ignored Xvfb copy is used when available.
Create a private prefix at `build/prefix-hidden`, or set `ADAPTER_WINEPREFIX`.
The first local prefix was copied from the original prefix.
Never run test games on the owner desktop.

Test the exclusive window coordinates and primary display:

```
make build/test_display.exe
(cd build && ../tools/hidden_display.sh timeout 45s wine ./test_display.exe)
```

Inspect imports without external Python packages:

```
python3 tools/inspect_pe.py /path/to/game.exe
```

Compare corresponding replay frames:

```
python3 tools/compare_frames.py original-captures adapter-captures
```

Each folder needs packed RGBA files and `frames.json`.
Each entry has `frame`, `width`, `height`, and `file` fields.
The frame field must identify the same replay checkpoint in both runs.
The tool requires 20 frames by default and rejects any changed byte.
This tool does not capture frames or align recordings.

Unsupported methods print their names and caller addresses, then stop.
The wrapper currently handles DirectDraw version 1, surfaces, palettes, and basic clipper state.
Windowed clip regions, GDI surface access, and other interface versions remain open.
Fullscreen clipper use requires the matching exclusive game window.
Do not use this DLL as a finished replacement yet.
