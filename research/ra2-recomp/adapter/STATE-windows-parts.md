# Windows parts state

Paused by owner on 2026-11-12. Check the existing redalert2-recomp project first.

## Done

- Commit 6a83bf7 makes MIT pcrecomp win32hle the base for all three public DLLs.
- ddraw.dll, dsound.dll, and binkw32.dll use one shared ra2adapter.dll and SDL2.dll.
- MIT notices and source versions are in upstream/pcrecomp/README.md.
- Windows API checks pass on the test-host. See reports/hle-api-checks.txt.
- All 65,536 RGB565 colours match Wine across 786,432 pixels.
- 6,144 stereo PCM16 test frames match the source exactly at unit gain.
- Intro and mission files decode at 640x480: 129 and 378 frames, both 15 fps.
- Repeated BinkDoFrame, pause returns, and movie-end state match the original DLL.
- Follow-up checks pass for both files after seeking back to frame one.
- Movie audio pause preserves queued samples. Resume consumes them. Seek clears them.
- Native surface, SDL presentation, and frame-comparison tests pass.
- The presenter borrows ra2ui's SDL window through RA2UI_SDL_WINDOW_ID.
- All tests use hidden Xvfb, private prefixes, dummy audio, and the PipeWire guard.

## Open

- J10 has no 20-frame skirmish match. No acceptance claim is made.
- J11 has no completed skirmish sound-method log.
- J12 has no accepted original-game intro plus mission playback result.
- J13 Blowfish is copied as a base. Its DLL and save storage are not built.
- The last game stopped at a startup CD-ROM notice. No skirmish frames were saved.
- The test observer now posts menu button notifications after controls appear.
- Its latest change also dismisses the startup notice. That change is not game-tested.
- The observer fixes the seed and measured FPS. Capture frames are 200 through 219.
- A baseline-against-baseline check is still needed if captures differ.
- GDI surface access stops with an explicit error.
- The older implementations remain as *-legacy.dll build targets.
- test-host API checks passed before the latest audio-pause and seek changes.
- Those latest changes passed on test-host. The last test-host file copy was stopped at pause.
- No game test from this helper remains active. Do not assume the test-host mirror is current.
- adapter/win32ui belongs to the J14 helper. This commit excludes its changes.

## Test commands

Run from adapter/ with the Windows SDL2 files and minimal FFmpeg build present:

```
make -j8 build/ddraw.dll build/dsound.dll build/binkw32.dll
make -j8 build/test_ddraw.exe build/test_display.exe build/test_dsound.exe build/test_bink.exe build/test_color.exe build/test_hle_stream.exe
make test test-tools
./tools/hidden_display.sh ./tools/check_windows.sh
(cd build && ../tools/hidden_display.sh wine test_hle_stream.exe)
```

Skirmish capture remains an unfinished test:

```
make build/capture_ddraw.dll build/capture_launch.exe
python3 tools/prepare_game.py --bink
flock -o /tmp/ra2-game-why.lock ./tools/hidden_display.sh ./tools/capture_skirmish.sh adapter
flock -o /tmp/ra2-game-why.lock ./tools/hidden_display.sh ./tools/capture_skirmish.sh wine
python3 tools/compare_surface_frames.py build/captures-wine build/captures-adapter
```

Keep the original game files unchanged. Never kill the shared original Wine prefix.
