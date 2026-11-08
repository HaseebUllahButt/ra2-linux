# SHP actor work

This is a partial actor renderer. It loads original art and rules INIs through
`IniFile`, and SHP data through `MixFileSystem`. The caller mounts the theater
and generic MIX archives first. `ActorArt::draw` returns an RGBA SHP canvas.
The caller supplies the palette, selected SHP direction, and sequence step.
`frame_at_tick` supplies a frame for one infantry sequence using its original
delay table. For game-speed-adjusted sequences, pass the adjusted delay.
It reports completion but does not choose the next sequence.

Evidence:

* `0x00523D00` reads infantry sequence first frame, length, facing stride,
  and optional fixed facing. `0x00518D80` selects the displayed frame using
  first + facing * stride + (sequence frame % count). `0x00518F52` uses
  the original facing table at `0x007EAEFC`. `0x006F6D94` converts the map
  facing byte to the engine direction.
* `0x0051D6F0` sets the sequence timer using the table at `0x007EAF7C`.
  `0x006F9E50` increments the current frame when the timer expires.
  The elapsed-tick helper assumes the actor updates on every game tick.
* `0x005F96B0` selects the theater letter in suitable shape names.
  `0x0045F230` and `0x005F9710` retry the generic `G` name.
  The default `T` code is temperate; the caller supplies other theater codes.
* `0x005349C0` loads the theater unit palette. The renderer accepts an existing
  palette rather than choosing it or producing house color conversion tables.

The `load_attached` function resolves named building animation keys, such as
`ActiveAnim`. `frame_at_tick` supports forward infinite loops with a fixed Rate.
`0x00427F66` converts Rate to a delay using integer 900 / Rate, and
`0x00423AC0` loops from LoopEnd back to LoopStart. The caller chooses whether
an attached animation is active. Reverse, ping-pong, random-rate, finite-loop,
and other animation states remain unsupported.
Terrain objects use the theater-specific extension from the original table.

`building_center` returns the original building center in world units from
cell and foundation dimensions. `building_draw_position` returns the separate
world point projected by the building draw caller. `infantry_position` uses
the five original subcell offsets. `project_world` applies the original fixed
height projection and the viewport offset. `main_canvas_origin` converts the
resulting screen anchor to a main SHP canvas origin. `0x00705E00` sets the
centered-draw flag, and `0x004AED70` subtracts half the nominal canvas size.
The caller supplies terrain height and any locomotor draw offset.
`object_y_sort` returns the original X + Y ordering key. `building_y_sort`
applies the Gate and TurretAnimIsVoxel adjustments from building rules.
Per-pixel depth remains unresolved.

`infantry_motion_sequence` selects the recovered standing and walking states,
including prone and water-capable branches. The caller supplies those states.
The code does not yet recover combat and other automatic state transitions,
fixed-facing changes, automatic building animation overlays,
health-dependent frames, team-color tables, lighting, or shadow compositing.
Effects accept an explicit SHP frame.
Do not treat this as a complete in-game actor renderer. `0x0044F820` skips an
unregistered building type. These two unresolved names are registered; their
final draw behavior is not established.

The audit reads 161 extracted original maps, using the original art and rules
files. It merges each map's type section over its base type section. It decodes
frame zero for each distinct infantry, structure, and terrain type, theater,
and map-local type override combination.

The all-theaters availability audit resolves 960 of 960 combinations. The
isolated audit also resolves 960 of 960 combinations. The isolated run builds a
new filesystem per theater and mounts its base, extension, ISO MD, and ISO
archives after the generic archives. Snow additionally mounts SNOWMD.MIX.

`0x00672660` registers names from BuildingTypes even without a rules section.
`0x0045F230` falls back to the type name when Image is absent, applies theater
substitution, then retries the generic `G` variant. For New Urban, CALA02 and
CALOND02 resolve to CNLA02.SHP and CNLOND02.SHP. `0x0044F820` skips a truly
unregistered building type.

Run the real actor test:

    c++ -std=c++17 -O0 -Wall -Wextra -Werror -I rebuild/src/fs rebuild/src/actors/{actors,test_main}.cpp rebuild/src/gfx/{palette,shp_decode}.cpp rebuild/src/rules/ini_file.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp -o /tmp/actors_test
    /tmp/actors_test /path/to/user/ra2/game

Run the map audit:

    c++ -std=c++17 -O0 -Wall -Wextra -Werror -I rebuild/src/fs rebuild/src/actors/{actors,map_audit}.cpp rebuild/src/gfx/{palette,shp_decode}.cpp rebuild/src/rules/ini_file.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp -o /tmp/actors_audit
    /tmp/actors_audit /path/to/user/ra2/game analysis/extracted/maps

Run the isolated theater audit:

    /tmp/actors_audit /path/to/user/ra2/game analysis/extracted/maps --isolated
