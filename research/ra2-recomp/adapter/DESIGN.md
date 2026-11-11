# Windows parts adapter

Status: J10 core works on Linux. The game acceptance checks have not run.

## Layout and build rules

Keep all adapter code and reports in this folder. Do not change the lifter or runtime.
Use C11 for portable state and pixel work. Use SDL2 for display, audio, and time.
Keep COM layouts, stdcall exports, and DLL names in thin Windows wrappers.
Build the same core as a Linux library. No wrapper can call the real replacement DLL as a fallback.
An unsupported method must log its interface, name, arguments, and caller, then stop the test.
Expected capability queries can return a documented unsupported result without stopping.

The owner approved FFmpeg for J12 on 2026-11-12 PKT.
The rebuild Bink player needs libavformat, libavcodec, libavutil, libswscale, and libswresample.
Use a Windows x86 build for the drop-in DLL. Linux-only tests are permitted if that build blocks J12.
Linux-only playback cannot prove that the original game accepts our Bink ABI.

## Evidence and limits

`reports/imports-static.json` reads the original game.exe import descriptors.
It records the executable hash, import addresses, and direct call/jump byte patterns.
The game imports DirectDrawCreate at 0x79a048 and DSOUND ordinal 1 at 0x79a050.
It imports 13 decorated Bink functions. It does not import Blowfish.dll directly.
COM activation and DLL exports must therefore be checked for J13.

`reports/com-methods.txt` lists possible COM methods and their 32-bit offsets.
The list comes from the installed Wine headers. It is an ABI inventory, not a use trace.
A static import table cannot show calls through COM pointers.
J4 must confirm methods, interface versions, formats, flags, and thread use.

Disassembly gives these first confirmed calls:

| Site | Interface | Method | Byte offset |
|---|---|---|---|
| 0x495a15 | IDirectDraw | SetCooperativeLevel | 0x50 |
| 0x495a6f | IDirectDraw | Release | 0x08 |
| 0x495b3a | IDirectDraw | GetCaps | 0x2c |
| 0x495b85 | IDirectDraw | GetDisplayMode | 0x30 |
| 0x495d09 | IDirectDraw | SetDisplayMode, three arguments | 0x54 |
| 0x495dfe | IDirectDraw | CreatePalette | 0x14 |
| 0x495ea7 | IDirectDraw | RestoreDisplayMode | 0x4c |
| 0x4960b8 | IDirectDraw | WaitForVerticalBlank | 0x58 |
| 0x4093d6 | IDirectSound | SetCooperativeLevel | 0x18 |

The DirectDraw object lives at game address 0x8523cc.
The DirectSound object lives at game address 0x8315ac.
Do not infer a surface interface from a slot offset alone.

## J10: DirectDraw

Own surface pixels in C memory. Preserve the game's bit depth, masks, aligned pitch, and palette.
Expose the same memory through Lock. Keep drawing exact before uploading an RGBA display buffer.
Use SDL_CreateWindowFrom for the original game window. Do not drain its Windows message queue.
Implement each confirmed interface with its exact argument list. Do not share incompatible vtables between versions.
Track references for surfaces, attached back buffers, palettes, and clippers.
Implement fill, copy, source/destination color keys, overlap, and nearest-neighbor scaling in C.
Add flip chains, clipping regions, lost-state changes, and surface DC work when traces prove their behavior.
GDI GetDC is a risk: the game may draw directly into the DirectDraw surface through GDI.
The portable core must accept those pixels without importing Windows display code.

Capture before display scaling. Compare visible pixels without pitch padding.
Record frame number, width, height, format, pitch, palette, and raw bytes.
Use identical replay inputs and fixed capture points for both runs.
Twenty arbitrary screenshots do not prove equality. Corresponding replay frames must match.
Acceptance: original game completes a skirmish with our DLL; 20 corresponding frames match every pixel.

## J11: DirectSound

Implement IDirectSound and IDirectSoundBuffer with shared PCM storage and separate playback cursors.
Lock can return two spans at a ring boundary. Unlock must publish writes to the SDL mixer safely.
Use SDL audio locking between game calls and the audio callback.
Match cursor movement, looping, status, frequency, logarithmic volume, pan, and buffer duplication.
The game already decodes its audio. Do not repeat BAG, AUD, or WAV decoding inside the DirectSound wrapper.
Acceptance: effects and music play in a skirmish; no unsupported method appears in the log.

## J12: Bink

Match all 13 names and their stdcall argument bytes from the import report.
Check game reads from the public BINK structure before selecting a structure layout.
The game also passes an existing DirectSound object to BinkOpenDirectSound.
Decode each frame through FFmpeg. CopyToBuffer must preserve destination pitch, rectangle, and output format.
Implement frame timing, next-frame state, seek, pause, volume, and error lifetime.
Compare the intro and one mission movie with the original DLL: width, height, and frame count must match.
Keep frame pixel comparison separate: decoder conversion can differ even when counts match.

## J13: Blowfish and save storage

Check DllGetClassObject, class IDs, interface IDs, and method layouts from the actual Blowfish DLL and activation sites.
Reuse verified arithmetic only after checking the rebuild source provenance.
Run known Blowfish vectors, MIX header decryption, and original game startup.
Implement compound storage as a separate portable C module. Do not replace it with a private save format.
Acceptance: our save loads in the original, and an original save loads with our storage code.

## References and licence record

- cnc-ddraw: https://github.com/FunkyFr3sh/cnc-ddraw
- Reference commit: 279a057ee7e1e4d584b56141c014ab01f3c6ee6d
- Its root LICENSE is MIT. Preserve its notice when copying substantial source.
- Wine surface reference: https://github.com/wine-mirror/wine/blob/master/dlls/ddraw/surface.c
- Wine buffer reference: https://github.com/wine-mirror/wine/blob/master/dlls/dsound/buffer.c
- Those Wine files state LGPL-2.1-or-later. No Wine implementation is copied into this core.
- Rebuild references: ../ra2-test-host/rebuild/src/{gfx,app/platform.cpp,audio,video/bink_player,fs} under the parent project folder.
- The rebuild uses C++ and FFmpeg. Its files cannot be dropped unchanged into a C-only portable core.

Reference checkouts stay ignored under `.reference/`. They are not shipped.
No distribution licence is selected for the new adapter code yet.

## Measured Windows progress, 2026-11-12

The compiler is `../tools/wcc`, supplied by the translation lead.
SDL2 Windows x86 version 2.30.12 builds and loads with the DLL under Wine.
The build must select `libSDL2.dll.a` explicitly. Zig otherwise selects the static archive and lacks its system dependencies.
The synthetic Windows test passes COM identity, lifetime, mode, surface, lock, fill, copy, color-key, palette, and flip-chain checks.
A first original-game probe stopped at CreateClipper. Basic clipper state is now implemented.
A second probe stopped at fullscreen clipper use at game caller 0x004ab6eb.
The wrapper now accepts the matching exclusive window as the full-surface clip region.
Windowed clipping still stops explicitly.
The next original-game probe reached the intro with no unsupported call before its time limit.
The owner saw a wrapped movie image on the desktop. All later game tests use a hidden Xvfb display and a private prefix.
All test runs disable Wine audio drivers and select SDL dummy audio. Sound tests must write output to a file.

The game passes ClientToScreen coordinates to BinkCopyToBuffer.
A stored display size alone leaves the original window at a desktop offset.
The exclusive-mode wrapper now places the game window at 0,0 and sets its client size through SDL.
A Windows test starts the window at 120,80, then proves origin 0,0 and client size 640x480 after SetDisplayMode.
The test also presents a primary surface on the hidden display.
This fixes the measured coordinate prerequisite. The 20-frame acceptance check remains open.

OpenTS read-only reference: https://github.com/OpenTS-Developers/OpenTS
Reference commit: ba3c6c560e7dccb7eb2147431f542c43fe4e2352.
Its LICENSE.md states GPL-3.0-or-later, with EA Section 7 terms for marked derived files.
Its DSurface currently uses GDI-backed buffers and fixed RGB565. Its SDL window code uses SDL3.
Use those files to find names and layouts. Check each against RA2 before reuse.
No OpenTS implementation is copied into this adapter.
The hidden 55-second probe reached a movie frame. Private captures show a coherent centered image without the earlier wrap.
The process reached the probe time limit with no unsupported-method log.
This is a visual check, not a Wine pixel-equality result.
