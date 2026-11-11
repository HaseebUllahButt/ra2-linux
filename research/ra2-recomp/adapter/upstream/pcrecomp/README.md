# win32hle base

These files come from the MIT projects below. Their licence notices are included here.

- pcrecomp: https://github.com/sp00nznet/pcrecomp
  Commit: 91998d6d1be9482946a23b7f441062e27d88a152.
  Files: runtime/win32hle/ddraw.c, ddraw_int.h, dsound.c, bink.c.
- redalert2-recomp: https://github.com/sp00nznet/redalert2-recomp
  Commit: 4ecdea4dd12729696d5fe0a4296c49202695bc3e.
  File: src/linux/blowfish.c.

The Windows DLL build uses these implementations through the native ABI bridge in ../../compat/ and ../../src/hle/.
The tests in ../../tests/ remain the acceptance checks.

Local changes:

- DirectDraw releases replaced clippers and palettes and frees the surface's clipper reference.
- The Windows clipper obtains the native window's visible region.
- DirectSound preserves exact PCM16 samples at unit gain and supports file capture.
- The Windows Bink bridge owns duplicated file descriptors and closes them.

Blowfish is copied as the J13 base. It is not built into a replacement DLL yet.
