# Voxel reader and experimental drawing

Build and test from the repository root:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror rebuild/src/voxel/{voxel,test_main}.cpp rebuild/src/gfx/palette.cpp rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp -o /tmp/voxel_test
/tmp/voxel_test
```

The test reads HTNK.VXL, HTNK.HVA and VOXELS.VPL from `/path/to/user/ra2/game`.
The VXL layout follows the original loader at 0x00755DB0. It reads a 32-byte header, 770 bytes per palette, 28-byte section headers, a body, and 92-byte section tails. HVA loading follows 0x005BD5C0: a 24-byte header, 16-byte names, and 48-byte matrices in frame order. VPL uses the shipped table and palette layout.

Drawing is experimental. It selects a VPL table directly. It does not compute original normal lighting, team colors, shadows, depth rules, voxel coverage, or original camera projection. It does not draw turrets or barrels automatically. HVA translation scaling and section transforms need original-engine verification. The image is not a complete game renderer.
