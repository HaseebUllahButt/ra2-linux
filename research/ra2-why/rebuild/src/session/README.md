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

This is not a complete skirmish session. House creation, credits, initial
units, placement, observer handling, random waypoint fallback, and RNG state
handoff to `Simulation` are not implemented. Start must remain disabled.

Run the native checks from the project root:

    make -C rebuild session

The test reads the real MP02T2 map from MULTI.MIX and both original executable
distance tables. It does not write game assets.
