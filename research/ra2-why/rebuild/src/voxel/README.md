# Base-game voxel drawing

Build and test from the repository root:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror rebuild/src/voxel/{voxel,test_main}.cpp rebuild/src/gfx/palette.cpp rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp -o /tmp/voxel_test
/tmp/voxel_test
```

The tests read original assets from `/path/to/user/ra2/game`. The API is in `voxel.h`.

Use `vehicle_pose_from_map()` to set hull and turret facings from a map facing byte. It also sets the initial barrel tilt to `0x4000` and the default `TurretOffset` to zero. Replace the offset with the unit type's rules value when present. Set hull, turret, and barrel model/animation pairs. Pass unscaled animations from `read_hva()`. `draw_vehicle()` scales private copies, selects the base-game light by default, and returns an RGBA image with its offset from the map object anchor. Set `original_lighting` to false to supply per-part normal light tables.

Original base-game machine code at `0x6C4700` initializes hull facing at actor `+2E0` and barrel tilt. `0x6FC95E` initializes the separate turret facing at `+2F8` from the same map byte. Drive curves update `+2E0` at `0x4A1B08`. The hull matrix reads that field at `0x53D04A`. The type constructor at `0x6D9B40` initializes `TurretOffset` to zero. The rules loader at `0x6DD9CC` reads its override. Camera setup is at `0x718C7E`. Hull facing is at `0x53D030`. Turret and barrel transforms are at `0x43AF89` through `0x43B10E`. Their draw-order choice is at `0x43B14A`. Light setup is at `0x7170D0`; light initialization is at `0x717FB0`.

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

`draw_vehicle_parts()` returns separate hull, turret and barrel surfaces in
the original order. Each `SceneImage` contains the original source rectangle
on its 256 by 256 surface. Its destination rectangle starts at
`left + source_x, top + source_y` relative to the vehicle anchor.
Do not derive terrain depth from the combined `draw_vehicle()` bounds.

On clear, flat ground with the tested locomotor,
`base_flat_vehicle_depth_adjustment(projected_height)` returns
`-projected_height - 1`. The input is the original `0x6A2970` result for
world Z. `base_flat_vehicle_depth_row()` combines this adjustment with the
original row inputs. It does not calculate clipping or world Z projection.
`base_flat_part_depth()` clips one part vertically against a full-map terrain
surface. It returns the first source row, row count and first row depth. It
uses Z base `0x8000` and origin Y zero. Advance the row with
`base_depth_next_row()`. Clip X in the scene. Supply the projected world Z.

`base_mark_bridge_gaps()` applies the base loader's `0x568B00` gap pass to a
512 by 512 row-major grid. Supply bridge span records produced by the original
`0x54FDA0` scan. It marks four cells across each gap, excluding its endpoints.
Unknown bridge cells produce unknown output where they can affect the result.
The scene must also track later bridge damage and intact bridge marker flags.

`base_mark_shadow_casters()` applies the original `0x52B8B0` offset table and
`0x472D20` flag pass. Supply the five group starts and caster tiles from the
theater loader. The caller must initialize the marker grid with flags from
other sources, including bridges. A missing caster group is represented by
zero, matching the loader. The original base TEMPERAT.INI, SNOW.INI and
URBAN.INI contain no active `ShadowCaster` entries.
The full adjustment at `0x4C9930` also checks terrain flags, nearby tile
heights, tile shapes, bridges and object state. These checks remain unsupported.
`supports_base_flat_vehicle_depth()` checks the narrow tested case. Supply a
row-major 5 by 5 neighborhood and the vehicle state. The guard rejects
unknown tube and terrain-marker values. `bridge` covers cell bit `0x100`,
`secondary_bridge_flag` covers `0x400`, and `marked_terrain` covers `0x10000`.
Vehicle state records
the two unclassified original object bytes and the `Harvester` rule. Unit
occupancy does not by itself reject this path.
The flat test executes the base game's default scale initialization. The
result is 0.14350360082660085, the same scale used by `actors::projected_height()`
in the parent scene.

```
python3 rebuild/src/voxel/verify_flat_depth_x86.py /tmp/voxel_test
python3 rebuild/src/voxel/verify_part_depth_x86.py /tmp/voxel_test
python3 rebuild/src/voxel/verify_static_flags_x86.py /tmp/voxel_test
```

This executes `0x4C9930` and its original map checks for 640 vehicle height,
ground level and facing cases. It also checks 1,080 changes to other cell flag
bits. The synthetic map supplies distinct flat cells and a 60 by 30 tile.
Two cells contain unit-list pointers. The checked path does not read those
lists. Only cell lookup, object position, facing and fixture access use stubs.
The second test executes the original part blitter and clipping code for 256
part positions, source bounds, projected heights and map-edge clips.
The third test checks bridge gaps, the complete ShadowCaster offset table,
and the three shipped base theater settings against original x86 and assets.

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
