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
The test compares 21 search traces and complete RNG states per edition against
the original x86 code. The map and object checks are supplied test inputs.

`starting_unit_budget` filters resolved starting types and computes the shared
unit budget. Base `0x005B7CE0` and expansion `0x005D6D80` exclude BaseUnit
vehicles, average eligible costs with original rounding, and multiply by the
unit count. Unknown required inputs and arithmetic overflow return no result.

This is not a complete skirmish session. House creation, final cash, additional
units, actual map and unit placement checks, observers, and random waypoint
fallback remain incomplete. Start must remain disabled.
`Simulation.load_random_state` can restore the advanced setup RNG before ticks
or queued commands. The complete session setup must supply that state.
