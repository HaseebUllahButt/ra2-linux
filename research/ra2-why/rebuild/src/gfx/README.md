# Palette and TS/RA2 SHP reconstruction

Clean C++17, with no third-party dependencies. The library owns its input bytes
and produces a full nominal canvas of row-major RGBA bytes. All game/archive
access in the test uses the finished `../fs/` layer and opens files read-only.
No network sources were needed. All source changes, build outputs, and generated
images for this task stay in this directory.

## Build and run

Run from `/tmp/r2` (the repository symlink) or `/path/to/user/ra2`:

```sh
TMPDIR="$PWD/rebuild/src/gfx" g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror \
  -I rebuild/src/gfx -I rebuild/src/fs \
  rebuild/src/gfx/palette.cpp rebuild/src/gfx/shp_decode.cpp \
  rebuild/src/gfx/test_main.cpp rebuild/src/fs/filename_hash.cpp \
  rebuild/src/fs/mix_archive.cpp rebuild/src/fs/mix_bootstrap.cpp \
  -o rebuild/src/gfx/gfx_test
rebuild/src/gfx/gfx_test
```

`rebuild/src/gfx/gfx_test --self-test` runs only the synthetic tests, without
reading the installation. Do not link `fs/test_main.cpp`, which has its own main.
The real test writes `rebuild/src/gfx/decoded_frame.ppm` (binary P6, black behind
transparent pixels). It checks nonzero opaque coverage, more than one opaque
RGB color, and positive summed RGB channel variance; differing alpha alone
cannot satisfy the variance check.

The requested loose `/path/to/user/ra2/game/conquer.mix` does **not** exist in
this installation. The test uses it if present; otherwise `MixFileSystem`
bootstraps the real archives and opens the nested `CONQUER.MIX` with the existing
`MixArchive` implementation. Here its backing offset is **196988** and it has
**289** directory entries. `UNITTEM.PAL` also resolves through those MIX mounts
and contains exactly **768 bytes**. No archive is extracted to disk.

MIX entries have full-filename hashes, without stored names or a separately
searchable extension hash. The test reads `scripts/binary_names.txt`, selects
literal `.SHP` candidates, adds guesses including `HVES.SHP` and `HVEA.SHP`, and
calls `MixArchive::find(name)` to hash the entire name with the finished CRC
implementation. Those two vehicle guesses do not resolve here; the other
candidates provide real SHPs.

## Local evidence and address map

Addresses are virtual addresses in the supplied gamemd.exe decompilation.
Names below describe observed operations, not recovered original symbols.
`analysis/ghidra-dump/strings.csv`, `keyword_refs.csv`, and `call_edges.csv`
were used to find and connect the resource cluster. The community cross-check
was read: its MIX addresses agree with the finished filesystem layer, but it
supplies no SHP decoder address proof. Format evidence below comes from the
local C files, not a community format description.

| Decompiled address / source | Observed evidence | New implementation |
| --- | --- | --- |
| [0x00545150](../../decomp/0054/FUN_00545150.c) | Constructs `ISO%s.PAL`, reads `0x300` bytes, shifts each stored byte left two. | `palette.cpp`: exact 768-byte input and channel expansion. |
| [0x005349C0](../../decomp/0053/FUN_005349c0.c) | Loads theater and `UNIT%s.PAL`; iterates `0x100` RGB-sized triples, shifting channels left two. | `palette.cpp`; real test's `UNITTEM.PAL`. |
| [0x005312A0](../../decomp/0053/FUN_005312a0.c), [0x005D2E90](../../decomp/005d/FUN_005d2e90.c) | `GLSMD.PAL` and font PAL paths independently repeat the 256-triplet/shift conversion. The former reads SHP nominal dimensions at +2/+4. | Corroborates `palette.cpp`, `ShpHeader`. |
| [0x0069E430](../../decomp/0069/FUN_0069e430.c), [0x0069E580](../../decomp/0069/FUN_0069e580.c) | Reads an 8-byte file header into a reference object; sets in-memory marker `-1`. Resolver distinguishes that object from a loaded blob and reads its bytes. | `ShpImage::parse` accepts loaded blobs only; no pointer/reference serialization. |
| [0x0069E740](../../decomp/0069/FUN_0069e740.c) | Count is `*(short *)(base+6)`; record is `base+8+frame*0x18`; data offset is a dword at record+0x14, added to blob base. Zero offset returns null. | `shp_decode.cpp`: header/table/offset parsing and absent frames. |
| [0x0069E7E0](../../decomp/0069/FUN_0069e7e0.c) | Reads four shorts at record+0/+2/+4/+6 into a rectangle. | `ShpFrame`: crop position and dimensions. |
| [0x0069E860](../../decomp/0069/FUN_0069e860.c), [0x0047C060](../../decomp/0047/FUN_0047c060.c) | Returns three record bytes at +0x0C..+0x0E; terrain color path consumes that triplet. Calling it `radar_color` is an inference from this use. | `ShpFrame::radar_color`, retained without PAL scaling. |
| [0x0069E900](../../decomp/0069/FUN_0069e900.c) | Returns `(record[8] & 2) >> 1`, reading a **byte**, not a dword. | `ShpFrame::flags` and `scanline_rle()`. |
| [0x004AED70](../../decomp/004a/FUN_004aed70.c) | Calls rectangle/data/flag accessors; adds crop offsets, uses nominal dimensions for centering; flag test selects raw or scanline drawing. | `decode_frame`: positioning and encoding dispatch; exports to a nominal canvas. |
| [0x004373B0](../../decomp/0043/FUN_004373b0.c), [0x004912B0](../../decomp/0049/FUN_004912b0.c), [0x004913F0](../../decomp/0049/FUN_004913f0.c) | Raw surface path advances pixel rows using width; raw blitters consume one source byte per pixel. `0x004912B0` skips index zero; `0x004913F0` has an opaque copy mode. | Raw decode; transparent-zero export policy described below. |
| [0x00437A10](../../decomp/0043/FUN_00437a10.c) | Row starts at a ushort pointer; passes `row+1` to blitter; next row is `byte_address(row)+*row`. | Scanline length includes its own two-byte word. |
| [0x00491B40](../../decomp/0049/FUN_00491b40.c), [0x004965B0](../../decomp/0049/FUN_004965b0.c) | Zero consumes the next byte as a skip; nonzero consumes one pixel and palette lookup. Loop continues while remaining width is positive. | Zero-run decoder, including terminal runs beyond crop width. |
| [0x005B40B0](../../decomp/005b/FUN_005b40b0.c) | Cached resource loader can return loaded file bytes or construct an SHP reference via `0x0069E430`. | Existing filesystem supplies bytes; decoder owns its blob instead of rebuilding the cache. |
| [0x007162F0](../../decomp/0071/FUN_007162f0.c), [0x00712170](../../decomp/0071/FUN_00712170.c) | Reads `Cameo`/`AltCameo`, falls back to `XXICON`, and calls `0x005B40B0`. | Establishes cameo resource connection. |
| [0x0047F6A0](../../decomp/0047/FUN_0047f6a0.c), [0x0047FB90](../../decomp/0047/FUN_0047fb90.c) | Terrain drawing calls `0x004AED70`; terrain bounds use the SHP rectangle accessor. | Establishes terrain resource connection. |

Concrete call-edge chains include `007162f0 -> 005b40b0 -> 0069e430`,
`0047f6a0 -> 004aed70 -> {0069e7e0,0069e740,0069e900}`, and
`004aed70 -> 00437a10`. The final scanline blitter call is **virtual** in
`0x00437A10`, so there is no direct blitter edge to infer from `call_edges.csv`.
The blitter bodies themselves supply the zero-run semantics.

## Format established by those routines

All multibyte disk values are little-endian. The byte order is also exercised
by the actual MIX-resident SHPs; parsing uses explicit byte loads.

| File byte offset | Size | Meaning |
| --- | --- | --- |
| 0 | 2 | Loaded TS/RA2 blob marker, zero in the real assets. |
| 2 | 2 | Nominal canvas width. |
| 4 | 2 | Nominal canvas height. |
| 6 | 2 | Frame count: **16 bits**, not a 32-bit count following a six-byte header. |
| 8 | count × 24 | Frame records. |

The decompile distinguishes `-1` as an in-memory reference marker; it does not
prove that every other marker is invalid. Requiring zero is this library's
supported-format policy, corroborated by the real files. The game's accessors
sign-extend the dimensions/count. This library stores valid sizes as unsigned
16-bit values and rejects values above 32767 rather than interpreting negative
sizes or counts as large allocations.

| Offset within each 24-byte frame record | Size | Meaning |
| --- | --- | --- |
| 0, 2 | 2 each | Signed crop x, y. |
| 4, 6 | 2 each | Crop width, height. |
| 8 | 1 | Encoding flags; bit `0x02` selects length-prefixed zero-run scanlines. |
| 9..11 | 3 | Uninterpreted. Real records sometimes contain `0xCC` bytes here. |
| 12..14 | 3 | Color triplet used by the terrain color accessor. |
| 15..19 | 5 | Uninterpreted by this implementation. No encoded-length claim is made. |
| 20 | 4 | Absolute payload offset relative to the start of the SHP, or zero for absent data. |

**Bit 0x02 clear:** read `crop_width * crop_height` palette indices, with
contiguous rows and no per-row length word. Both flag values **0 and 1** occur
in real decoded assets. No meaning for their bit-0 difference is asserted.

**Bit 0x02 set:** each row starts with a `u16` byte length including those two
bytes. Within the row, a nonzero byte is one literal palette index; `0, n`
skips `n` transparent pixels. This is zero-run encoding, not repeated-color
RLE or a tile directory. Flag **3** occurs in real assets. The dispatch code
also sends flag **2** to this same path; the claimed `2 = uncompressed
scanlines` interpretation is contradicted by `0x0069E900` and `0x004AED70`.
Flag 2 is covered by an exact fixture, but no flag-2 frame occurred among the
named real files tested, so this is a control-flow result, not a real-asset
observation.

The blitter stops when remaining width is zero **or negative**. For example,
`CHRONOSK.SHP` frame 0 has crop width 33 but many rows finish with transparent
runs expanding the row to 34 pixels. The decoder accepts that terminal
overshoot, never writes those extra positions, and locates the next row using
its length word. Bytes remaining in a row after width is satisfied are ignored,
as in the blitter; a zero-count skip consumes input and is harmless if the row
subsequently covers its width. Tokens needed to cover width must remain inside
the declared row. These details have fixtures alongside the real-archive test.

## API and deliberate boundaries

```cpp
auto palette = ra2::gfx::Palette::from_bytes(palette_member_bytes);
auto shp = ra2::gfx::ShpImage::parse(shp_member_bytes);
auto rgba = shp.decode_frame(0, palette);
// rgba.width, rgba.height, rgba.pixels (R,G,B,A bytes)
```

`Palette::load_file(path)` handles standalone `.pal` files in binary read-only
mode. Both palette entry points require exactly 768 bytes. Channel expansion
is `uint8_t(channel << 2)`: 63 becomes 252, without bit replication to 255.
Larger source bytes wrap as the original byte stores do. Palette entries have
alpha 255; palettes themselves contain no alpha channel.

The SHP export makes index zero transparent black in either encoding and leaves
outside-crop/absent pixels transparent black. This is an explicit RGBA export
policy consistent with transparent blits, not a claim that every game drawing
mode treats zero identically. Signed crop positions are clipped against the
nominal canvas. Each frame decodes independently. This layer does not implement
game lighting, player remaps, depth buffers, shadow blending, or compositing;
`UNITTEM.PAL` is the test's chosen display palette, not an automatically inferred
palette for every SHP. Earlier TD/RA SHP formats and serialized in-memory
reference objects are unsupported.

Header/table extents and all nonzero payload offsets are checked at parse time;
row/raw payload extents and token completeness are checked when decoding.
Payloads are bounded by the next distinct nonzero frame offset, or file end.
That boundary is a defensive inference because the accessors establish no
encoded-size field. Shared offsets and out-of-order records work. Trailing
file/frame alignment bytes are permitted. Null offsets and zero-sized crops
produce blank frames. A zero-sized nominal canvas is valid (`NULL.SHP`). Canvas
and crop areas default to at most 16,777,216 pixels each; callers may choose a
different limit in `parse`. All format errors throw `GraphicsError`.

## Verification on this installation

The warning-clean optimized build and the AddressSanitizer/UndefinedBehaviorSanitizer
build both run the exact fixtures and the real archive. The fixtures cover all
four low flag combinations, exact palette/RGBA values, index-zero transparency,
positive and negative crop offsets, absent/empty frames, shared and unordered
payload offsets, scanline overshoot/padding, every truncated prefix of raw and
RLE fixtures, malformed lengths/tokens, cross-frame reads, invalid markers,
invalid offsets, out-of-range frame indices, and pixel limits.

| Named SHP | Nominal canvas | Frames decoded |
| --- | --- | --- |
| CHRONOSK | 48 × 92 | 13 |
| FOG | 60 × 30 | 96 |
| NULL | 0 × 0 | 2 |
| OREGATH | 144 × 80 | 120 |
| PIPBRD | 36 × 4 | 2 |
| PIPS | 10 × 15 | 19 |
| PIPS2 | 8 × 15 | 18 |
| PLACE | 60 × 59 | 38 |
| POD | 60 × 60 | 4 |
| PROGBAR2 | 147 × 21 | 1 |
| PROGBARM | 80 × 5 | 1 |
| SHROUD | 60 × 30 | 96 |

Total: **12 SHPs, 410 frames**; **145 active raw**, **145 active RLE**, and
**120 absent/empty** frames. All record flag-byte counts, including inactive
records: 0 = 205, 1 = 37, 3 = 166, 204 (`0xCC`, empty NULL records) = 2.
The preview is `OREGATH.SHP` frame 0: **75 opaque pixels**, **30 opaque RGB
colors**, summed channel variance **20293.1**. The PPM is **144 × 80**, with
34,560 RGB payload bytes; its header and payload size were independently read
back and checked. These tests exercise all named candidates that resolve, not
an assertion that every unidentified archive member has been decoded.

To reproduce the sanitizer run, replace `-O2` above with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer` and change the
output executable to `rebuild/src/gfx/gfx_test_sanitized`.
