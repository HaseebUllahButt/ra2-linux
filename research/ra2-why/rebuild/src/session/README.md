# Skirmish session primitives

`choose_start_point` implements regular player starting-point selection for
standard Battle mode. It accepts the simulation's shared `Random` object and
the original executable's `OriginalDistance` table. It updates the occupied
point flags only on success. It rejects invalid or unsupported coordinates.

Base `0x005B7920` uses the same regular-player selection rules. The
expansion mode virtual table at `0x007EE184` selects `0x005D6C70` for
slot +84 and `0x005D6890` for slot +C4. The first regular player gets a
random free point. Each later player gets the free point with the largest
sum of distances to occupied points. Equal sums keep the first point.
Observer handling and waypoint fallback need separate work. The legacy helper
`0x005EE6F0` has different rules and is not used here.

`select_base_unit` selects the first BaseUnit with the resolved country bit.
The mode calls `0x005D7030`, which uses `0x00505310`. The caller supplies
fully resolved Owner masks. An unknown earlier mask prevents selection. Real
rules select AMCV for Americans, SMCV for Russians, and Yuri PCV in expansion.

`initialize_house_values` copies the selected credits and color to the original
multiplayer house fields. It represents the initial assignment, before later
map and unit processing. Base `0x004E8300` and expansion `0x004FCE00` match.

`find_initial_placement` implements the original search order from base
`0x00660B60` and expansion `0x00688ED0`. The caller supplies resolved map
validation, occupancy, and placement callbacks. Base validates the origin;
Yuri also validates each retry. The callbacks share the simulation RNG.
The test compares 63 search traces and complete RNG states per edition against
the original x86 code. The map and object checks are supplied test inputs.

`starting_unit_budget` filters resolved starting types and computes the shared
unit budget. Base `0x005B7CE0` and expansion `0x005D6D80` exclude BaseUnit
vehicles, average eligible costs with original rounding, and multiply by the
unit count. Unknown required inputs and arithmetic overflow return no result.
`choose_starting_unit` selects one entry from already filtered vehicle and
infantry lists. It uses the shared RNG and rejects an empty required list.
`generate_starting_units` filters one resolved house's candidates, then runs
the original budget loop. A callback stages unit placement in private scratch
state. Unresolved callbacks and unsafe inputs return no plan and preserve the
caller RNG. The loop limits failed placements to 20. Zero-cost types and plans
above 100,000 units are unsupported. Add a positive remaining budget to house
cash only at the original caller's later step. After every house succeeds,
`finish_starting_unit_generation` consumes the original debug RNG draw.

`plan_battle` stages one regular single-player Battle setup. Its input must
contain resolved first-pass starts, slot and country data, and post-overlay mode
and type values. Its RNG must already be at the end of the first start pass.
The required callbacks stage world preparation, unit placement, four house
updates, and the remaining scenario initialization in private scratch state.
World preparation must confirm the second Battle start pass and unchanged
active houses, starts, and resolved type inputs. The final phase must confirm
that the staged houses, starts, type inputs, and units remain valid. Any unresolved result
aborts and leaves the caller RNG unchanged. Only a successful plan commits it.

The callbacks are not implemented here. House creation, base-unit missions, real
map and unit placement checks, observers, random waypoint fallback, and later
scenario behavior remain open. A synthetic callback test does not establish a
playable session. The simulation supports RNG state handoff separately. Start
must remain disabled.
`Simulation.load_random_state` restores setup RNG state before ticks or queued
commands. The complete session setup must supply that state.



Run the native checks from the project root:

    make -C rebuild session session-terrain


The test reads the real MP02T2 map from MULTI.MIX and both original executable
distance tables. It does not write game assets.

`PlacementTerrain` takes a complete simulation terrain snapshot. It rejects
missing or duplicate allocated cells. Unknown height or ramp values remain
unresolved. It does not check unit occupancy or create houses or units. The
MP02T2 test uses real map bounds and waypoints with fixture terrain values.
Use the snapshot from the original placement phase. A later terrain pass can
change those values.

`real_terrain_test.cpp` loads ALL02S.MAP and theater assets from the original
base game. It builds the full cell grid and runs the recovered first terrain
pass before checking every cell. The boundary comparison tests the adapter;
`--cases` emits the resolved cells for `original_geometry_check.py`. Both
original executables match all 15,525 real first-pass cell checks.
Run `test_real_terrain.sh GAME_DIRECTORY --original` for both comparisons.
The shared Makefile also builds the fixture once for direct comparisons:

    make -C rebuild build/session_terrain_test
    python3 rebuild/src/session/original_geometry_check.py game/game.exe rebuild/build/session_terrain_test game --cases
    python3 rebuild/src/session/original_geometry_check.py game/gamemd.exe rebuild/build/session_terrain_test game --cases
    python3 rebuild/src/session/original_geometry_check.py game/gamemd.exe rebuild/build/session_terrain_test game --cases --yuri C1A02MD.MAP

The optional original checks require Python Unicorn.
Set `RA2_SESSION_YURI_MAP=C1A02MD.MAP` to also load the original expansion map
from MIX files. Its 22,990 first-pass cells match the expansion executable.

`original_geometry_check.py` runs original map clipping and map boundary
instructions for 8,712 cases per edition. Build `geometry_cases.cpp` with
`map_geometry.cpp`. Supply each original executable and the case binary.

`multiplayer_house_color` converts a selected color to the original palette
index. `original_color_check.py` checks 17 inputs in each original executable.
The house field copy still uses the selected color before that conversion.
`auxiliary_house_types` gives the two later special houses and their named
color lookup. `original_auxiliary_check.py` checks their original order in
both editions. It supplies the constructors and color lookup results.

`unit_unlimbo_suffix` covers only the Unit code after its parent placement
succeeds. It returns the facing argument, conditional status, and timer. The
timer can consume a shared random draw. The parent placement and facing
handler are still required. `original_unlimbo_check.py` checks 320 cases per
edition. It supplies both parent outcomes and observes the facing argument.

For the optional original x86 comparison, build `chooser_cases.cpp` with
`session.cpp`, `sim/random.cpp`, and `sim/distance.cpp`. Run
`original_chooser_check.py game.exe chooser_cases` and repeat for `gamemd.exe`.
It needs Python Unicorn and `sim/original_image.py`. It runs 502 cases per
edition. It compares the selected point, flags, random indexes, and all random
state words. It replaces only debug logging.

`original_base_unit_check.py` compares the native BaseUnit choice with the
original selector. Build `base_unit_cases.cpp` with `session.cpp` and the same
simulation sources. It runs 320 cases per edition. The country resolver is
stubbed to provide the already resolved country index. The selector itself runs.

`original_house_check.py` compares the multiplayer house field assignment with
the original x86 instructions. Build `house_cases.cpp` with `session.cpp` and
the same simulation sources. It runs 56 cases per edition without stubs.

`original_placement_check.py` compares placement search traces against the
original code with supplied map and object results. Build `placement_cases.cpp`
with `session.cpp` and the same simulation sources. The real map occupancy and
unit placement checks still need integration.

`find_initial_placement_checked` distinguishes an exhausted search from an
unknown check. Unknown checks leave the shared random state unchanged. Its
callbacks must use private scratch world state until the whole plan succeeds.

`original_budget_check.py` compares the budget prefix with original x86. Build
`budget_cases.cpp` with `session.cpp` and the same simulation sources. It tests
192 cases per edition. The type Cost virtual returns the supplied resolved cost;
the original filter and arithmetic instructions execute.

`original_unit_choice_check.py` compares the extra-unit choice boundary with
original x86. Build `unit_choice_cases.cpp` with `session.cpp` and the same
simulation sources. It tests 327 cases per edition, including every RNG word.
The original decision and RNG execute with supplied filtered candidate lists.

`original_unit_loop_check.py` compares the extra-unit budget loop with the
original x86 code. Build `unit_loop_cases.cpp` with `session.cpp` and the same
simulation sources. It checks 27 cases per edition. Creation, Cost, and
placement results are supplied. Original selection, retry, subtraction, final debug draw, and RNG instructions
execute.

`original_phase_check.py` runs the original second Battle-entry prefix and
unit driver in both editions. It stubs the mode methods and four house updates
to record their order. It verifies the second start pass precedes the unit
driver, and that each house gets base unit, extra units, and four updates
before the final debug draw. It does not verify the stubbed work.

`starting_unit_mission` records Guard for human extra units and Area Guard for
AI extra units. `original_mission_check.py` verifies both original x86 branches
in each edition with the house-control flag supplied. The base-unit mission is
still unresolved.

`world/setup.h` loads the installed base rules, art, map, and theater into an
owning asset object. It preserves packed records in original cell order.
`read_base_packed_map` and `packed_map_snapshot` cover only the map-reader
boundary after packed tiles, before tubes, overlays, and map objects. The
snapshot has `SetupProgress::PackedMapRead`; it cannot enter the live runtime.
The constructor only proves the low occupancy byte at this boundary, so the
full 32-bit occupancy fields remain unknown. No bridge or occupancy result
from this early phase may be used after later phases without executing them.

The original Battle start pass writes the selected cell center with Z zero.
`battle_start_position` follows its checked x86 instructions. The direct base
Unit helper stages a constructor, its first placement, and Cell setup together.
The retry helper stages the checked original radius search after a blocked
direct attempt. Both require real resolved type, map, and phase inputs. An
unresolved input leaves the snapshot and all 250 RNG words unchanged. A full
search failure stays unresolved because Unit destruction is not yet staged.
The asset-backed placement test supplies runtime fields and is not a live map
load. The map objects, overlay effects, complete type loading, House updates,
extra infantry, remaining scenario phases, and live handoff remain incomplete.
`construct_house_fragments` covers only constructor fragments; it omits calls
between houses and does not supply a complete House phase RNG state.

`world/original_battle_start_check.py` compares the native start coordinate
with base x86. The optional world resize check compares 19,900 MP02T2 cells
at the packed-map boundary. `original_real_map_check.py` also compares direct
placement at the real Battle start Z, with explicitly supplied runtime state.
