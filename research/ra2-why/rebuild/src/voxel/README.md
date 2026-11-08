# Voxel reader and experimental drawing

Build and test from the repository root:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror rebuild/src/voxel/{voxel,test_main}.cpp rebuild/src/gfx/palette.cpp rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp -o /tmp/voxel_test
/tmp/voxel_test
```

The test reads HTNK, HTNKTUR, MTNK, MTNKTUR and TNKD VXL/HVA pairs, and VOXELS.VPL, from `/path/to/user/ra2/game`.
The VXL layout follows the original loader at 0x00755DB0. It reads a 32-byte header, 770 bytes per palette, 28-byte section headers, a body, and 92-byte section tails. HVA loading follows 0x005BD5C0: a 24-byte header, 16-byte names, and 48-byte matrices in frame order. VPL uses the shipped table and palette layout.

Drawing is experimental. It accepts a single-light lookup or selects a VPL table directly. It does not compute original shadows, depth rules, voxel coverage, or original camera projection. It does not draw turrets or barrels automatically. HVA translation scaling and section transforms need original-engine verification. The image is not a complete game renderer.

Evidence for the file fields:

- `0x755DB0` reads the VXL counts and body size from the 32-byte header. It skips 770 bytes per embedded palette. It reads 28-byte section headers and 92-byte section tails. The first three tail words point into the body. The last four tail bytes carry section dimensions and normal type. The tail also contains scale, a matrix, and two 3D bounds.
- `0x756860` uses the section width and height with the span offset tables. The shipped files show the skip, count, color/normal pairs, and repeated count in each span. The exact span encoding still needs more original-code tracing.
- `0x5BD5C0` reads HVA frame and section counts, skips 16 bytes per section name, and reads 48 bytes per matrix. `0x5BD730` scales matrix translation components.
- `0x758A30` reads the 16-byte VPL header, 768 palette bytes, and 256 bytes per lighting table. Header words zero and one select the reserved color range. The third word is the table count. The fourth word remains unknown.
- `0x758670` selects normal vectors by type, clamps a negative dot product to zero, multiplies by 16, and rounds. Its last three special entries use level 16. The original tables are at `0x846AC8` and `0x846F78`; their counts are at `0x8469F4`. The compiler decompilation omits the multiply; machine code at `0x7586B9` shows it.

The caller must supply a light vector in section coordinates. Original projection and pixel coverage need `0x753F90`, `0x754510`, `0x756860`, and `0x756590`. Shadow drawing uses `0x706BD0` and `0x707280`; its full transform and surface rules remain open. A visible test image does not verify any of these behaviors.

`scale_hva_translations` implements `0x5BD730`. Its caller `0x5F8110` supplies the first VXL section scale. Apply it once after loading each VXL/HVA pair. The experimental `draw` function expects an animation already scaled by the caller.

`double_light` follows `0x7586F0` for two section-space light vectors and the original normal tables. The game transforms the lights before this call in `0x753D00`. Light levels are limited to the available 32 VPL rows by this API.

`make_default_lighting` follows `0x758B70` and `0x758EA0`. It selects the closest palette entry inside or outside the reserved range, according to the source index. `team_colors` replaces that reserved range. On the shipped VOXELS.VPL, the generated tables match all 8,160 nonzero-index bytes with the documented `-O2` build. An `-O1` build matches 8,159 bytes. The remaining byte needs a rounding investigation. The shipped tables remain available unchanged through `read_vpl`.

Full vehicle projection and coverage remain unresolved. `0x754510` sorts and draws sections. `0x756590` selects a raster routine from `0x846840`. Those routines include `0x7DF7C0`. The decompiler omits floating-point stack values in these paths. The current image is still experimental. Shadow transform and raster behavior also remain unresolved.

The VPL mismatch is row 14, source index 130. The source color is `(199, 175, 115)`. The shipped table selects index 177, `(255, 247, 111)`; the `-O1` build selects index 178, `(255, 235, 107)`. The documented `-O2` build selects 177. At the ideal factor 1.3, both squared distances equal 1876.19. Floating-point rounding changes the tie. The original VPL bytes remain unchanged when loaded.

`project_ground_corners` implements the ground-bound transforms in `0x753F90`. `ground_coverage` implements the unoccluded column mask in `0x756860`. Its fixed-point buffer is 256 by 256. It marks two horizontal bytes for every occupied column. At the last byte, it clips the second write to keep the result bounded. The original supports a separate occlusion surface, which this API does not implement.

`verify_coverage_x86.py` uses Unicorn to execute the original functions from `/path/to/user/ra2/game/gamemd.exe`. It checks one synthetic transform and three coverage cases. The C++ test checks the same expected results. Run the optional check with a Python environment that has `unicorn` installed:

```
python3 rebuild/src/voxel/verify_coverage_x86.py
```

These checks do not establish full vehicle projection or voxel pixel coverage. The non-depth raster paths now have original-code comparisons. Depth-buffer and whole-vehicle composition still need reconstruction and original-code comparisons.

`raster_section` implements the non-depth indexed paths selected by `0x756590`, including `0x7DF7C0`, `0x7DF8C0`, and the per-normal VPL variants. It takes the caller's complete section-to-view matrix. `raster_rgba` applies the selected palette, including team colors, and treats index zero as transparent. It does not assemble a whole vehicle or reproduce the game's camera and depth-buffer paths.

The tests compare all 65,536 indexed pixels by hash for eight HTNK directions, with and without VPL lighting. They also compare two lit MTNKTUR views. These 18 expected hashes come from original x86 execution. Reproduce them with Unicorn and the compiled test executable:

```
python3 rebuild/src/voxel/verify_raster_x86.py /tmp/voxel_test
```

The original raster converts fixed-point values by truncation. The ground coverage helper now uses the same conversion.

`game_camera()` returns the game's default voxel camera matrix. Original startup code at `0x7558CE` applies X rotation −60 degrees and Z rotation −45 degrees. The optional x86 test executes the original rotation helpers for those values.

`facing_transform()` follows `0x43E085` through `0x43E0E3`: it rounds the 16-bit facing to 32 directions, rotates around Z, and applies the integer X offset divided by eight. `barrel_transform()` follows `0x43E439` through `0x43E52D`: it translates around the stored pivot, applies facing and tilt rotations, and restores the pivot. The optional x86 test checks a synthetic pivot. The full hull, turret, and barrel setup still needs reconstruction. The Ghidra function export omits this original x86 routine; its machine code is present.

`draw_scene()` accepts ordered model and animation pairs, complete part matrices, frames, and per-part light tables. It combines HVA section matrices, sorts each part's sections by projected depth, and draws into one 256 by 256 image. It returns the image and its top-left position relative to the screen anchor. Scale each HVA translation once with `scale_hva_translations()` before use. Callers supply complete part transforms and the original draw order. The API does not yet calculate the full hull, turret, or barrel transforms. Multi-part output and depth-buffer behavior need original-game comparison. The image position matches the original `0x754510` output for the tested HTNK transform.

`draw_vehicle()` creates the part matrices for a flat, stationary base-game vehicle. Supply the hull, turret, and optional barrel VXL/HVA pairs in `VehiclePose`. Supply 16-bit hull and turret facings, barrel tilt, `TurretOffset` from the rules, and a light table for each part. The neutral barrel tilt is `0x4000`. The input HVA values must be unscaled values returned by `read_hva()`. The function scales a private copy and wraps the requested frame. It returns RGBA pixels and a screen offset. The caller can omit the turret and barrel, or supply both files for each part.

The base game uses slightly different trigonometric lookup values from standard sine and cosine. `base_game_camera()` and `base_facing_transform()` use values checked against `/path/to/user/ra2/game/game.exe`. Its original camera setup is at `0x718C7E`. Its hull facing code is at `0x53D030`. Turret and barrel setup is at `0x43AF89` through `0x43B10E`. The barrel order decision is at `0x43B14A`. `verify_base_x86.py` checks the original camera, 32 facing values, 32 barrel tilt values, two complete turret/barrel matrix cases, and 32 draw-order cases. It uses Unicorn and the base game executable.

Vehicle-level pixel alignment remains unverified against a complete original game frame. Slopes, moving suspension, recoil, building turrets, terrain occlusion, and the game's depth-buffer paths are not implemented by `draw_vehicle()`. The automatic hull transform is for a flat stationary unit. The 256 by 256 surface limits the size of a vehicle image.
