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

This is not a complete skirmish session. House creation, final cash, additional
the complete unit generation loop, actual map and unit placement checks,
observers, random waypoint fallback, and RNG state
handoff to `Simulation` are not implemented. Start must remain disabled.

For this helper checkout, build the test against the parent simulation headers:

    c++ -std=c++17 -O0 -Wall -Wextra -Werror -I /path/to/user/ra2/rebuild/src/app rebuild/src/session/{session,test_main}.cpp /path/to/user/ra2/rebuild/src/sim/{random,distance}.cpp /path/to/user/ra2/rebuild/src/rules/ini_file.cpp rebuild/src/fs/{filename_hash,mix_archive,mix_bootstrap}.cpp -o /tmp/session_test
    /tmp/session_test /path/to/user/ra2/game

The test reads the real MP02T2 map from MULTI.MIX and both original executable
distance tables. It does not write game assets.

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

`original_budget_check.py` compares the budget prefix with original x86. Build
`budget_cases.cpp` with `session.cpp` and the same simulation sources. It tests
192 cases per edition. The type Cost virtual returns the supplied resolved cost;
the original filter and arithmetic instructions execute.

`original_unit_choice_check.py` compares the extra-unit choice boundary with
original x86. Build `unit_choice_cases.cpp` with `session.cpp` and the same
simulation sources. It tests 327 cases per edition, including every RNG word.
The original decision and RNG execute with supplied filtered candidate lists.
