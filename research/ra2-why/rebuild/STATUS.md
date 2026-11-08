# Rebuild status

| layer | state | verified |
|---|---|---|
| fs (MIX archives) | done | yes - all 16 archives, encrypted headers, key cross-check |
| rules (INI) | done | yes - real rulesmd.ini, 65+80+403+105+12 entries |
| text (CSF) | done | yes - both tables, 9,690 strings |
| loop (game loop) | done | yes - six phases, 100 frames, sanitizer clean |
| gfx (SHP/palette) | done | yes - 12 SHPs, 410 frames, PPM preview |
| hook (DLL) | skeleton | trampoline tested; Windows cross-compile pending mingw |
| naming | done | 2,160 functions named: 1 community + 2,159 evidence-inferred |

One command: make test. Every finished layer compiles and passes
against the real game data.
