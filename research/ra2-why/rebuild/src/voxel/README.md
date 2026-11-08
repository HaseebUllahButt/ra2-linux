# Base-game voxel drawing

Build and test from the repository root:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror rebuild/src/voxel/{voxel,test_main}.cpp rebuild/src/gfx/palette.cpp rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp -o /tmp/voxel_test
/tmp/voxel_test
```

The tests read original assets from `/path/to/user/ra2/game`. The API is in `voxel.h`.

Use `vehicle_pose_from_map()` to set hull and turret facings from a map facing byte. It also sets the initial barrel tilt to `0x4000` and the default `TurretOffset` to zero. Replace the offset with the unit type's rules value when present. Set hull, turret, and barrel model/animation pairs. Pass unscaled animations from `read_hva()`. `draw_vehicle()` scales private copies, selects the base-game light by default, and returns an RGBA image with its offset from the map object anchor. Set `original_lighting` to false to supply per-part normal light tables.

Original base-game machine code at `0x6C4700` initializes turret facing and barrel tilt. `0x6FC95E` initializes hull facing from the same map byte. The type constructor at `0x6D9B40` initializes `TurretOffset` to zero. The rules loader at `0x6DD9CC` reads its override. Camera setup is at `0x718C7E`. Hull facing is at `0x53D030`. Turret and barrel transforms are at `0x43AF89` through `0x43B10E`. Their draw-order choice is at `0x43B14A`. Light setup is at `0x7170D0`; light initialization is at `0x717FB0`.

`draw_vehicle()` draws each part on its own surface before combining them. This matches the original caller's part alignment for two tested flat HTNK poses. The x86 comparison executes the original base-game hull, turret, barrel, camera, lighting, and non-depth raster code. It compares every visible pixel and location for the three shipped HTNK parts, with turret offsets zero and 16. These two poses do not prove every vehicle or game state. Slopes, suspension, recoil, building turrets, terrain occlusion, and depth-buffer paths remain open.

Run the original-code comparisons with Python and Unicorn installed:

```
python3 rebuild/src/voxel/verify_base_x86.py
python3 rebuild/src/voxel/verify_raster_x86.py /tmp/voxel_test
python3 rebuild/src/voxel/verify_vehicle_x86.py /tmp/voxel_test 0
python3 rebuild/src/voxel/verify_vehicle_x86.py /tmp/voxel_test 16
```

The base-game tests cover camera values, all 32 facing and tilt values, turret/barrel order, two caller matrix cases, four lighting tables, and initial map facings. The raster tests compare eight HTNK views with and without lighting and two MTNKTUR views against original x86 hashes. The full-vehicle tests compare the flat HTNK hull, turret, and barrel against original base-game x86 output at two offsets.

VXL layout follows the original loader at `0x755DB0`. HVA loading follows `0x5BD5C0`, and `0x5BD730` scales its translation components. VPL loading follows `0x758A30`. Lighting preserves the original normal vector tables and VPL reserved color range. The loader retains shipped VPL bytes. `make_default_lighting()` can differ by one byte at `-O1` due to a floating-point tie; the documented `-O2` build matches all 8,160 tested nonzero-index bytes.

`raster_section()` implements non-depth indexed section drawing. `raster_rgba()` applies the palette. `ground_coverage()` implements the unoccluded ground-column mask; it clips a second write at the end of the surface. `draw_scene()` accepts explicit part matrices. Multi-part alignment in `draw_scene()` remains unverified; use `draw_vehicle()` for the tested flat vehicle path. The older `draw()` remains experimental.

`base_shadow_section()` returns a 256 by 256 shadow mask and its screen-relative origin. Supply the section matrix before the camera transform. The function applies the base-game camera and original shadow offset. Its optional callback returns true when a surface pixel blocks the shadow. `surface_x` and `surface_y` are the original caller's surface offsets before the projected center is added. The callback coordinates and per-pixel mask match the original base-game surface checks in `0x719C10` for the tested cases. Map-wide terrain surface construction remains open.

`raster_section_depth()` returns indexed color and depth planes and their screen-relative origin. Both planes are 256 by 256. It implements the base-game depth-enabled section raster, including its strict depth comparison and two-pixel writes. These depth bytes are relative to the section raster. Their conversion to the terrain's map-wide depth surface remains open. The function does not accept a pre-existing terrain depth plane.

The normal base-game vehicle raw blitter uses a separate terrain depth test.
`base_depth_row()` reproduces gradient record 2 at `0x434C64`.
Use the original Z-buffer base, buffer Y origin, destination Y, clipped source Y,
clipped height, and caller adjustment. Call `base_depth_next_row()` after each
screen row. `base_depth_visible()` applies the raw blitter test at `0x488870`.
It draws a nonzero pixel only when the row value is less than the terrain word.
It does not update the terrain word. For a reversed terrain buffer `C - Z`,
the corresponding vehicle value is `C - row.value`; it must be strictly greater
than the stored terrain value. Both values must use the same `C`.

`0x6D27AD` calls the vehicle's gradient selector. `0x4C9A10` returns record 2
when there is no locomotor; a locomotor can select another record. The caller
adjustment comes from `0x4C9930`. Its full map inputs and the shared constant
`C` are not yet recovered. Do not use section-local depth bytes as terrain
depth. The RLE depth reader uses a different drawing path.
The locomotor interface table at `0x7A31EC` selects gradient 2 through
`0x53D4B0` and contributes zero to the adjustment through `0x53D4A0`.

The following test compares 256 record 2 cases and five raw blits with the
original base-game x86 instructions. It checks transparent pixels, ties,
depth limits, row progression, and that the terrain buffer stays unchanged.

```
python3 rebuild/src/voxel/verify_map_depth_x86.py /tmp/voxel_test
```

Run the full original x86 sweep with Unicorn and the compiled test executable:

```
python3 rebuild/src/voxel/verify_sweep_x86.py /tmp/voxel_test
```

It checks 32 flat vehicle facings for HTNK, MTNK, and 4TNK. It checks HTNK again with a turret offset of 16. Each vehicle comparison uses the original light and compares all visible pixels and positions. It also checks unoccluded and surface-occluded ground masks at 32 facings for each hull, plus eight depth views for each hull. The shadow sweep tests each first VXL section with its facing matrix; it does not apply its HVA animation. The vehicle sweep applies each shipped HVA matrix and its translation scale.
