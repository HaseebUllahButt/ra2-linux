# Rebuild status

| layer | state | verified |
|---|---|---|
| fs (MIX archives) | done | yes - all 16 archives, encrypted headers, key cross-check |
| rules (INI) | done | yes - real rulesmd.ini, 65+80+403+105+12 entries |
| text (CSF) | done | yes - both tables, 9,690 strings |
| loop (game loop) | done | yes - six phases, 100 frames, sanitizer clean |
| gfx (SHP/palette) | done | yes - 12 SHPs, 410 frames, PPM preview |
| hook (DLL) | phase 3 proven | Bootstrap hook wired + bridge-tested |
| naming | 3 batches done: 3095 of 8584 named (36%); batch 4 queued |

One command: make test. Every finished layer compiles and passes
against the real game data.
