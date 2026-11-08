# Voxel reader and experimental drawing

Build and test from the repository root:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror rebuild/src/voxel/{voxel,test_main}.cpp rebuild/src/gfx/palette.cpp rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp -o /tmp/voxel_test
/tmp/voxel_test
```

The test reads HTNK, HTNKTUR, MTNK, MTNKTUR and TNKD VXL/HVA pairs, and VOXELS.VPL, from `/path/to/user/ra2/game`.
The VXL layout follows the original loader at 0x00755DB0. It reads a 32-byte header, 770 bytes per palette, 28-byte section headers, a body, and 92-byte section tails. HVA loading follows 0x005BD5C0: a 24-byte header, 16-byte names, and 48-byte matrices in frame order. VPL uses the shipped table and palette layout.

Drawing is experimental. It accepts a single-light lookup or selects a VPL table directly. It does not compute original double lighting, team colors, shadows, depth rules, voxel coverage, or original camera projection. It does not draw turrets or barrels automatically. HVA translation scaling and section transforms need original-engine verification. The image is not a complete game renderer.

Evidence for the file fields:

- `0x755DB0` reads the VXL counts and body size from the 32-byte header. It skips 770 bytes per embedded palette. It reads 28-byte section headers and 92-byte section tails. The first three tail words point into the body. The last four tail bytes carry section dimensions and normal type. The tail also contains scale, a matrix, and two 3D bounds.
- `0x756860` uses the section width and height with the span offset tables. The shipped files show the skip, count, color/normal pairs, and repeated count in each span. The exact span encoding still needs more original-code tracing.
- `0x5BD5C0` reads HVA frame and section counts, skips 16 bytes per section name, and reads 48 bytes per matrix. `0x5BD730` scales matrix translation components.
- `0x758A30` reads the 16-byte VPL header, 768 palette bytes, and 256 bytes per lighting table. Header words zero and one select the reserved color range. The third word is the table count. The fourth word remains unknown.
- `0x758670` selects normal vectors by type, clamps a negative dot product to zero, multiplies by 16, and rounds. Its last three special entries use level 16. The original tables are at `0x846AC8` and `0x846F78`; their counts are at `0x8469F4`. The compiler decompilation omits the multiply; machine code at `0x7586B9` shows it.

The caller must supply a light vector in section coordinates. Double lighting at `0x7586F0` remains open. Team color selection needs the range handling in `0x758B70` and `0x758EA0`. Original projection and pixel coverage need `0x753F90`, `0x754510`, `0x756860`, and `0x756590`. Shadow drawing uses `0x706BD0` and `0x707280`; its full transform and surface rules remain open. A visible test image does not verify any of these behaviors.
