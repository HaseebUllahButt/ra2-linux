# Native battlefield terrain renderer

Implemented `src/render/tmp_decode.{h,cpp}`, `minimap.{h,cpp}` and
`test_main.cpp`. No existing pack, graphics, archive, map or decompile files
were changed. The renderer reads actual packed terrain and archive artwork;
it does not enlarge the map's embedded PreviewPack.

Run from `/tmp/r2`:

```sh
bash rebuild/src/render/run.sh
# Optional read locations:
bash rebuild/src/render/run.sh /tmp/r2/game /tmp/r2/analysis/extracted/maps
# Parser-only checks, no archives required:
rebuild/src/render/render_test --self-test
```

The script builds its binary and places compiler temporary files inside
`rebuild/src/render/`. It links the existing fs, gfx palette, rules INI and
pack implementations. The pack reader requires the existing Linux
`liblzo2.so.2` runtime. All PPMs are binary P6, RGB, max value 255, with names
`rebuild/src/render/{2PEAKS,DOWNTOWN,FRSTBITE}.ppm`. The caller can convert
these to PNG. The root Makefile was deliberately not changed.

## Evidence chain from packed cells to TMP drawing

The primary evidence is the local game decompile, not a community format
description. No network access was needed.

* `decomp/0056/FUN_0056bac0.c` reads each cell's tile index into cell `+0x38`,
  translates it with `0x00544E30`, then reads subtile `+0x11A` and elevation
  `+0x11B`. `decomp/0054/FUN_00544e30.c` applies the old tile-set index shifts.
* `decomp/0048/FUN_00480350.c` resolves the tile type through `DAT_00A8ED2C`,
  chooses a tile variation, subtracts `15 * cell.height` from the draw Y,
  and calls `0x00547CF0`. Its reconstructed C prototype has missing/misplaced
  register arguments, so follow the data uses in the callee as well.
* `decomp/0054/FUN_00545150.c` reads `[TileSet%04d]`, `TilesInSet`,
  `LastTilesInSet` and `FileName`, constructs numbered art names, loads the
  file, and fixes up the offset table beginning at file `+0x10`. Each
  **file** gets a global tile index; the subtiles inside that file do not
  increment the global index. Alphabetic variation files are separate links.
* `decomp/0054/FUN_00547020.c` repeats that offset-table fixup in the lazy
  loader. The number of entries is the product of the first two header
  words. Zero offsets remain absent subtiles.
* `decomp/0054/FUN_00544c20.c` checks the selected subtile exists.
  `FUN_00547150.c` explicitly reads header words 2 and 3 as pixel dimensions.
* `decomp/0054/FUN_00547cf0.c` selects the subtile through
  `header[4 + subtile % (header[0]*header[1])]`, takes its diamond pixels
  from `record + 0x34`, and separately draws its rectangular extra plane.

## Container header

All integers below are little endian. Offsets are **file-relative** in the
subtile table, and **record-relative** in each subtile's optional planes.
The native implementation retains offsets rather than mutating them to
host pointers.

| File offset | Type | Meaning | Original evidence |
| --- | --- | --- | --- |
| `0x00` | u32 | Subtile grid columns | `00545150`, `00547020` multiply header words 0 and 1 |
| `0x04` | u32 | Subtile grid rows | Same; `00543330` iterates columns inside rows |
| `0x08` | u32 | Diamond canvas width, 60 | `00547150`: header word 2 |
| `0x0C` | u32 | Diamond canvas height, 30 | `00547150`: header word 3 |
| `0x10` | u32 array | `columns*rows` subtile offsets; zero means absent | Loader fixups and `00544C20` |

## Subtile record

| Record offset | Type | Meaning | Original evidence |
| --- | --- | --- | --- |
| `0x00`, `0x04` | i32, i32 | Image origin X, Y in the TMP container | `00547CF0` subtracts these from extra origin |
| `0x08` | u32 | Extra rectangle palette-index plane offset | `00547CF0`: `piVar4[2] + record` |
| `0x0C` | u32 | Diamond Z-plane offset | `00547CF0`: `piVar4[3] + record` |
| `0x10` | u32 | Extra rectangle Z-plane offset | `00547CF0`: `piVar4[4] + record` |
| `0x14`, `0x18` | i32, i32 | Extra rectangle origin X, Y | `00547CF0`: extra placement is `(extra_x-x, extra_y-y)` |
| `0x1C`, `0x20` | u32, u32 | Extra width, height | `00547CF0`: `piVar4[7]`, `piVar4[8]` |
| `0x24` | u32 | Flags; bit 0 extra, bit 1 Z planes, bit 2 variation marker | `00547CF0` tests bits 0/1; `005471F0` tests bit 2 |
| `0x28` | u8 | Relative tile height | `00543330` adds/subtracts it when placing/removing a tile |
| `0x29` | u8 | Terrain classification | `00544BE0` indexes terrain lookup with this byte |
| `0x2A` | u8 | Ramp classification | `005471B0` returns this byte |
| `0x2B..0x2D` | RGB bytes | Left radar color | `00549E90`, `0047C060` |
| `0x2E..0x30` | RGB bytes | Right radar color | `00549E90` |
| `0x31..0x33` | 3 bytes | Unused/padding in the examined drawing path | Diamond starts at `0x34` |
| `0x34` | 900 bytes | Raw diamond palette indices | `00547CF0`, the diamond mask and real files |

These are TMP **format** files with theater suffixes `.TEM`, `.SNO`, `.UBN`,
etc.; looking up only filenames ending in `.TMP` would miss the real art.
There is no SHP or LCW decompression in this TMP drawing path.

`00547CF0` uses a 60-column, 29-row diamond mask at `0x007EC4C8`, builds
scanline lookup tables from it, and consumes contiguous pixels from
`record+52`. To corroborate the decompile's static data, read-only PE
address mapping of `game/gamemd.exe` confirmed the mask's row widths:

```text
4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
56,52,48,44,40,36,32,28,24,20,16,12,8,4
```

They sum to 900. Each row is centered in a 60-pixel canvas; the nominal
30th row is transparent. Diamond palette index zero is **opaque**, matching
the unconditional pixel lookup in the diamond loop. Extra pixels are raw,
rectangular, row-major indices; that loop explicitly skips index zero.
Z planes use the same packed layout as their corresponding color planes.

For example, real `CLEAR01.TEM` is 1872 bytes: 16-byte header, one 4-byte
offset, then a 52-byte record, 900 diamond indices and 900 Z bytes. Its
record-relative Z offset is 952. Its inactive extra fields contain
uninitialized-looking `CD` bytes; flag tests must precede reading those
fields as dimensions or offsets. All unknown flag bits are preserved.

The decoder bounds-checks the grid/table, every record and active plane,
extra dimensions, relative placement and allocations. Subtiles are bounded
by the next distinct record offset or EOF. Sparse tables, duplicate offsets
and non-monotonic table order are supported. Absent/out-of-range referenced
subtiles fail explicitly. Only the verified RA2 60x30 geometry is accepted.

## Theater lookup and palettes

`005349C0` constructs theater MIX names from the 0x70-byte theater records
at `0x007E1B78`. `00545150` separately selects `ISO<extension>.PAL` and
`<root>MD.INI`. The six theater records and the short fallback strings were
also inspected read-only in the executable to resolve constants omitted
from the strings CSV. The renderer currently enables the three needed here:

| Theater | INI | Theater/ISO MIXes (MD before base) | Terrain palette |
| --- | --- | --- | --- |
| TEMPERATE | TEMPERATMD.INI | TEMPERAT[MD], ISOTEMMD, ISOTEMP | ISOTEM.PAL |
| NEWURBAN | URBANNMD.INI | URBANN[MD], ISOUBNMD, ISOUBN | ISOUBN.PAL |
| SNOW | SNOWMD.INI | SNOW[MD], ISOSNOMD, ISOSNOW | ISOSNO.PAL |

Generic `ISOGEN[MD].MIX` is also mounted. Optional MD archives may be absent.
Every map gets a fresh bootstrapped `MixFileSystem` so previous theaters
cannot affect lookup order. Numbered art names follow `FileName` plus a
minimum two-digit 1-based number and theater extension. `0xFFFF` and
`0xFFFFFFFF` tile sentinels use the configured ClearTile set. Catalog
translation implements `LastTilesInSet`; the three shipped INIs used here
have zero such shifts.

One needed SNOW bridge, `Ovrpsb02.SNO`, is in **SNOW.MIX**, not ISOSNOW.MIX.
This is why mounting only the ISO archives is insufficient. The non-editor
loader's final `.URB` fallback (`00545150`, `LAB_0054673A`, string at
`0x00829138`) is implemented when `NonMarbleMadness != 0`. No missing-image
placeholders or cross-theater substitutions were needed for these maps.

The palette is likewise not necessarily in the ISO archive: the actual
`ISOTEM.PAL` and `ISOSNO.PAL` come from CACHE.MIX; `ISOUBN.PAL` comes from
CACHEMD.MIX. The code uses the existing archive search and `gfx::Palette`,
requiring exactly 768 bytes and expanding each six-bit channel with
`channel << 2` (maximum 252). This matches the palette loop in `00545150`.
Radar RGB metadata is preserved separately and is not used to color terrain.

## Projection, dimensions and depth

`006D1F10` projects world coordinates with 60/2 horizontal and 30/2 vertical
factors. `00565C10` establishes valid cells with `W < x+y <= W+2H` and
`abs(x-y) < W`. `00578460` applies LocalSize bounds using twice its offsets
and dimensions. Thus the native rectangular view uses **60 pixels per
LocalSize column and 30 pixels per LocalSize row**, not 24 pixels.

For Size width W and LocalSize `(L,T,LW,LH)`, the diamond's top-left is:

```text
screen_x = (x-y+W-1)*30 - L*60
ground_y = (x+y-W-1)*15
screen_y = ground_y - cell.height*15 - T*30
output   = (LW*60) by (LH*30)
```

Every record is considered, including outside LocalSize: elevated art and
overhangs can enter the crop. The compositor applies extra origins relative
to the cell, not the whole TMP container. It sorts back to front for
deterministic ties and uses per-pixel Z planes for overhang occlusion.
The original depth expression is `global - screenY - 30 - height*15 + z`;
reversing its sign and dropping common constants gives `ground_y+30-z`,
with larger values nearer. Bitmap alpha handles canvas padding and extra
index-zero transparency; uncovered pixels write black to PPM.

## Verification

Built with C++17, `-Wall -Wextra -Werror -O2`. Parser checks cover the exact
diamond footprint, index-zero behavior, palette expansion, extra placement,
extra transparency, depth data, absent subtiles, truncation, bad offsets,
bad dimensions and inactive garbage fields. The real test asserts expected
dimensions, more than 50 colors, channel variance above 100, at least 98%
coverage and a complete PPM byte count. Every referenced tile resolves to
real artwork; errors stop the test instead of silently dropping cells.

| Map | Theater | Tile records | Loaded TMPs / decoded subtiles | PPM dimensions | RGB colors | Opaque pixels | Summed channel variance |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: |
| 2PEAKS | TEMPERATE | 16,218 | 213 / 691 | 4560 x 2700 | 232 | 12,312,000 / 12,312,000 | 6006.07 |
| DOWNTOWN | NEWURBAN | 15,105 | 179 / 864 | 4560 x 2460 | 230 | 11,217,600 / 11,217,600 | 9238.57 |
| FRSTBITE | SNOW | 19,898 | 243 / 826 | 5700 x 2610 | 235 | 14,875,965 / 14,877,000 | 17370.7 |

Visual inspection of downscaled copies confirmed coherent islands,
shorelines, cliffs, ramps, streets, plazas, railways and bridge terrain.
The native PPMs remain available for the caller's PNG conversion.

## Open work

* This is the battlefield **terrain-art view**. Trees, buildings, units,
  overlay ore, bridge overlays and other SHP/object layers are not drawn.
* Scenario lighting, theater tint, shadow passes and animated water/terrain
  frames are not reproduced. The raw ISO palette is used without remapping.
* The base artwork variation is deterministic. The game's alphabetic
  variants and randomized spatial variation selection (`004814F0`) remain
  open. Editor marble art is outside this runtime renderer.
* FRSTBITE has 1,035 uncovered pixels (0.00696%), retained as black in PPM;
  exact in-game handling of those terrain/object seams remains open.
* URBAN, DESERT and LUNAR mounting/catalog selection and TS 48x24 tile
  geometry are not enabled. Invalid or unsupported geometry is rejected.
* Pixel comparisons against an in-game screenshot with identical lighting
  and all object layers remain open; these outputs establish real tile
  decoding, catalog lookup, elevation, cropping and terrain composition.
