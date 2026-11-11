# RA2 Windows parts

The Windows DLLs use the MIT pcrecomp win32hle code as their base.
The native bridge calls it from the original 32-bit game.
The source and licence notices are in [upstream/pcrecomp](upstream/pcrecomp/README.md).
DirectDraw, DirectSound, and Bink API checks pass. See [the checks](reports/hle-api-checks.txt).
J10 still needs 20 matching skirmish frames. J11 still needs a full skirmish log.
J12 still needs the original-game movie check. J13 is not complete.

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
The base handles DirectDraw interfaces, surfaces, palettes, and native window clip regions.
GDI surface access remains open.
Fullscreen clipper use requires the matching exclusive game window.
Do not use this DLL as a finished replacement yet.

Builds may use eight jobs on test-host and twelve jobs on the test-host.
The hidden runner prefers `~/.local/bin/Xvfb` and selects a free display automatically.
The shared OpenTS reference is `/path/to/user/Projects/ra2/OpenTS`.

Compare a fixed pattern containing every RGB565 color:

```
make build/test_color.exe
(cd build && ../tools/hidden_display.sh ../tests/capture_color.sh wine)
(cd build && ../tools/hidden_display.sh ../tests/capture_color.sh adapter)
```

These are synthetic display checks. They do not satisfy the skirmish replay gate.

Extract and compare the captured pixels with ImageMagick and Python:

```
magick build/color-wine.png -depth 8 rgba:build/color-wine.rgba
magick build/color-adapter.png -depth 8 rgba:build/color-adapter.rgba
python3 tools/check_color_pattern.py build/color-wine.rgba build/color-adapter.rgba
```

Build the current DLL set with FFmpeg and the SDL2 Windows development files present:

```
make -j8 build/ddraw.dll build/dsound.dll build/binkw32.dll
./tools/hidden_display.sh ./tools/check_windows.sh
```

All three public DLLs need `ra2adapter.dll` and `SDL2.dll` beside them.
The shared backend keeps one audio mixer for effects, music, and movies.
The `*-legacy.dll` targets keep the earlier implementations for comparison.
The portable surface and conversion tests remain the pixel checks.
