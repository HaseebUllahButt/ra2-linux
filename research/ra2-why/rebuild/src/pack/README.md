# Native map pack reader

`make pack` checks damaged inputs and reads every original packed map under
`analysis/extracted/maps`. Linux needs the `liblzo2.so.2` runtime library.
The implementation does not use OpenRA or Windows code at runtime.

`MapFile::parse` now loads typed terrain through `packed_terrain()`.
It retains the original text for inspection. Terrain-only callers can use
`ra2::pack::load_terrain(IniFile)` directly.

## Evidence from the original program

- `rebuild/decomp/004a/FUN_004ace70.c` reads `IsoMapPack5` and calls
  `0x0056BAC0` to load its records.
- `0x0056BAC0` reads four coordinate bytes, four tile index bytes, and
  three individual bytes. `0x0056B3F0` writes the same fields and ends
  the stream with the four-byte empty coordinate. All 161 original
  packed maps use `00 00 00 00` for this terminator.
- `0x0055C720` constructs the terrain compression stream with an
  8192-byte block size. `0x0055C7C0` reads two little-endian 16-bit
  lengths, then the compressed payload. The safe Linux LZO1X decoder
  produces exactly each block's declared output length in all maps.
- `0x005FD2E0` reads `OverlayPack` and `OverlayDataPack` through
  `0x00552490`, with 512 rows and 512 columns. These are complete
  262144-byte arrays, rather than one 8192-byte block.
- `0x00552490` uses the same four-byte chunk framing and calls the
  LCW decoder at `0x00551C60`. Its literal, relative copy, absolute
  copy, long copy, fill, and terminator operations are implemented
  with explicit input and output bounds.

Each terrain record has little-endian x and y coordinates, a 32-bit tile
index, and three bytes for subtile, height, and ice data. The last byte's
ice meaning is an inference from the cell field; the reader preserves it.
Each preview is decoded through LZO and checked against its declared RGB
dimensions. The reader sorts numbered INI lines before joining their
base64 text, and rejects missing lines and invalid padding.

Tests cover all LCW commands, overlapping copies, truncated payloads,
invalid references, output limits, base64 padding, and section ordering.
The real-file check covers 161 maps, 2581554 tile records, both overlay
arrays for each map, and 6395622 bytes of preview pixels.

This module reads map data. It does not draw the battlefield or implement
unit behavior.
