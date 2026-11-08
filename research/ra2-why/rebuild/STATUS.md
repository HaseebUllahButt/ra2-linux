# Rebuild status: native game incomplete

The native Linux client opens original menu pages and maps.
It cannot start or complete a match.
Full movement, combat, production, AI, campaign scripts, saves, and multiplayer remain incomplete.

Function naming is complete for gamemd.exe: 8,584 entries.
Community headers support 1,410 names. The other 7,174 names are inferred.
These counts measure names, not implemented gameplay.

| layer | state |
|---|---|
| fs (MIX archives) | done, crypto verified 3 ways |
| rules (INI) | done, real data |
| text (CSF) | done, 9,690 strings |
| map | done, 161 maps, full INI structures |
| pack (LCW) | done, verified on all maps |
| loop (game loop) | done, six phases |
| gfx (SHP/palette) | done, real tiles render |
| hook (DLL) | phase 3 proven (Bootstrap bridge-tested) |
| symbols | 8,584 / 8,584 named - 100% |

The native client also has editable skirmish countries, colors, opponents,
credits, and map selection. Tests verify base tank routes on clear and road
cells, including final turns. Goal pathfinding remains incomplete.
Production-time calculations match both original executables.
Production commands and unit placement remain unsupported.

The original map setup audit passes 87 available base maps and 161 expansion maps.
The base packet lists 50 cooperative variants absent from the installed base mounts.

One command: make test. All layers pass against the real game data.
