# Native simulation core

This is a C++17 simulation foundation with verified state primitives.
It is not a complete RA2 engine or a playable game.
All changes stay under `rebuild/src/sim/`.

Run `rebuild/src/sim/test.sh` from any directory.
Pass an extracted asset directory as its first argument.
The default directory is `/path/to/user/ra2/analysis/extracted`.
No original asset changes or package installations are needed.
The existing pack reader needs the Linux `liblzo2.so.2` runtime.
The test script removes its temporary executable.

## Public API

Include `sim.h` and use `ra2::sim`.
Construct `Simulation(base_rules_ini, map_ini, seed)`.
Call `issue(Command)` to queue a native command.
Call `tick()` once for each requested simulation frame.
Call `snapshot()` to read an owning copy of state.
Read `types()`, `weapons()`, and `capabilities()` for rule inputs and limits.

A command has an absolute native frame and an issue ticket.
Commands due on one tick execute in issue order.
Future commands remain queued; past commands fail.
The last tick's results include each ticket, error, and returned value.
Execution errors do not change the affected state.
This schedule is a native integration contract, not the original binary event format.
The caller must not treat a successful empty tick as an original object AI update.

`ResolvedDamage` applies health arithmetic to vehicles and aircraft.
Its amount is already resolved, and bypasses armor and immunity.
It does not fire a weapon or reproduce derived damage handlers.
Positive amounts remove health; negative amounts restore health up to Strength.
Zero health marks a record dead. Dead records remain readable and cannot heal.
This record state does not reproduce original destruction effects.

`TakeCash` removes available cash and records the paid amount.
It supports houses with known cash and no stored ore.
`RandomRange` uses the recovered original random stream.
Attack, Produce, Stop, and unknown commands return `Unsupported`.
Move supports the limited original drive segment described below.
Unknown health or money produces a specific error.

The separate `Factory` primitive advances cash-only factory stages.
Its caller must supply an already resolved cost and build time.
`begin()` starts a job. `tick()` advances stages and charges cash.
It stalls when money is insufficient and resumes when money becomes available.
Completion suspends the factory at stage 54.
This primitive does not create or place an entity.

## Original evidence

Addresses refer to the supplied **gamemd.exe** analysis.
The symbol CSV supplies discovery names; decompiled bodies supply primary evidence.
The Base RA2 checks section records independently checked base addresses and specific behavior equivalences.

| Behavior | Evidence |
| --- | --- |
| Type Strength and Armor | `rebuild/decomp/005f/FUN_005f92d0.c`, 005F92D0 |
| Primary and Secondary references | `rebuild/decomp/0071/FUN_00712170.c`, 00712170 |
| Original map vehicle records | `rebuild/decomp/0074/FUN_00743270.c`, 00743270 |
| Vehicle map health | Original instructions 007435DA–00743616; health*Strength/256, truncate, snap within three points, zero becomes one |
| Health scale constant | Original little-endian double at 007E1740: 0.00390625 |
| Conversion mode | 007C5F00 uses the control word at 00822D80, value 00000E7F: truncate |
| House Credits*100 and player adjustment branches | `rebuild/decomp/0050/FUN_00500b40.c`, 00500B40 |
| Cash debit branch and spent amount | `rebuild/decomp/004f/FUN_004f9790.c`, 004F9790 |
| Forced health subtraction, healing cap, zero health | `rebuild/decomp/005f/FUN_005f5390.c`, 005F5390; arithmetic only |
| Factory stage interval | `rebuild/decomp/004c/FUN_004c9fb0.c`, 004C9FB0: clamp(build_frames/54,1,255) |
| Factory initialization and start timer | 004C9C70 and 004C9EA0 decompiled bodies |
| Factory stage update and cash charging | Original instructions 004C9B20–004C9C41; missing from supplied decompilation |
| Remaining timer frames | `rebuild/decomp/0042/FUN_00426630.c`, 00426630 |
| Random seed expansion | `rebuild/decomp/0065/FUN_0065c6d0.c`, 0065C6D0 |
| Random ring update and range rejection | 0065C780 and 0065C7E0 decompiled bodies |
| Seed constants | Original bytes at 00839644 and 00839694 |
| Frame/event ordering context | `analysis/engine-loop-network.md`; 0055D360, 0055AFB0, 00647260 |

The random seed expansion uses signed arithmetic shifts and unsigned wrap.
It has 250 words and initial indices 0 and 103.
It is not the CRT generator at 007CB4AA.
Equal range endpoints consume no draw. Reversed endpoints swap.
Ranges with signed overflow fail explicitly.

The optional `original_rng_check.py` checks the native test vectors against original x86 instructions.
It needs Python Unicorn and a read-only `gamemd.exe` path.
It executes only the constructor and random draw routines in an emulator.
It starts no game process and uses no Wine.

## Data limits

Type and weapon fields preserve authored text after map overlays.
Missing integer fields stay optional; unknown engine defaults are not guessed.
Weapon fields include Damage, ROF, Range, Projectile, and Warhead.
Cost and Speed are authored rule values, not final engine values.
Unrecovered armor selection, fire intervals, and projectile motion remain explicit gaps.
Drive Speed uses the original conversion and the supported terrain and country inputs described below.
Registry indexes are not original object indexes.
Native IDs follow Structures, Units, Infantry, Aircraft order.

Original map missions, facing, infantry subcells, house ownership, and cells are retained.
Partial vehicle health uses recovered instructions.
Partial health for other categories remains unknown.
Referenced houses absent from the map receive unknown cash and a reported gap.
Local player cash stays unknown because session adjustments need more state.
Triggers, tags, teams, transports, bridges, occupancy, terrain effects, and reinforcement scripts do not execute.
Decoded terrain inputs now support the limited adjacent Move command.
Map overlays follow the existing INI reader's documented first-key policy.

## Remaining recovery

General movement still needs the rest of 004B2630, linked Foot state, cell passability, and pathfinding handlers.
Target selection needs 006F8DF0 and its virtual eligibility/threat handlers.
004D9920 only wraps that selection. 006FCDB0 also applies transport and target eligibility rules.
Combat still needs projectile updates and derived damage/destruction handlers.
The forced health primitive must not replace those handlers.
Production still needs House::CanBuild, full queues, and placement handlers.
The separate build_time and prerequisite-list modules recover timing and list checks with resolved external inputs.
The factory primitive accepts resolved inputs to avoid inventing those numbers.
The native frame uses 64 bits and rejects overflow; the original frame counter wraps at 32 bits.
The core does not claim replay parity with original network events or original autonomous object updates.

## Recovered ground movement state

`ground.h` exposes `GroundMotion`, `GroundGuards`, and world coordinates.
These are verified movement primitives. The separate Move integration executes the limited map segment described below.
`GroundMotion::move_to()` ports the destination setter at 004AFD40.
The caller supplies four resolved Foot virtual results and the resolved bridge height adjustment.
Each true guard leaves the destination unchanged.
A cleared optional coordinate represents the original runtime empty-coordinate sentinel.
The code does not read an uninitialized data section as that sentinel's value.

The locomotor interface vtable at 007E7EB0 proves the dispatch:
slot +40 contains 004B0500, and slot +44 contains 004AFD40.
004B0500 calls 004B2630 to prepare paths and 004B0F20 to follow track samples.
The supplied decompilation omits 004B0500; its original instructions were inspected.
Complete-object offsets are four bytes above the interface offsets in 004AFD40.

`GroundMotion::transform()` ports 004B4780.
It uses all 64 original reflection flags from the table at 007E7B28.
It returns curve x, y, and facing, without inventing height.
`consume_one_path_direction()` ports the 23-dword forward shift in 004B2630.
The original leaves the last path entry unchanged.
The typed default uses an explicitly empty native path.
It does not claim a complete original constructor or save-state layout.

The native tests compare all 64 transforms with original x86 results.
`original_ground_check.py` reproduces these vectors with Unicorn.
The original routine leaves output z untouched; the evidence check verifies this too.

Ground movement still needs linked Foot virtual slots +1AC, +2CC, +480, +484, +534, +538, and +544.
Path planning at 004D3920, map lookup at 00565730/005657A0, and bridge/occupancy callbacks remain missing.
004B0F20 also needs sample transition tables, linked movement speed, and cell entry/exit effects.
Commands that only store a destination must not be reported as completed map movement.

Target recovery reached 006F8DF0 and its candidate predicate 006F7CA0.
The unbounded list branch keeps the first eligible candidate with the greatest signed score above -1.
The predicate spans 482 decompiled lines and depends on weapon choice, diplomacy, visibility, armor, range, and target virtual methods.
Bounded scans additionally call 006F8960 and map iterators.
A nearest-enemy rule would not reproduce this selection, so Attack remains unsupported.

## Recovered damage and target primitives

`combat.h` exposes `total_damage()` from 00489180.
Original instructions recover the floating-point operands omitted from its decompilation:

- +124 holds single-precision CellSpread; multiply it by 256 and truncate for the radius.
- +12C holds single-precision PercentAtMax; store scaled damage in single precision.
- Interpolate between initial and scaled damage by distance, then truncate.
- Clamp this intermediate damage to zero.
- Multiply by the selected double-precision armor verse at +A0+armor*8, then truncate.
- Cap the result at Rules +16C8, MaxDamage.
- Negative damage applies unchanged below distance eight and becomes zero at distance eight.
- Zero damage, an absent warhead, or scenario flag 20 returns zero.

The caller supplies resolved warhead inputs and the armor coefficient.
This function does not parse missing warhead defaults or choose armor automatically.
Interpolation supports nonnegative radii with the recovered 53-bit truncation mode.
Exceptional numeric inputs throw errors.
This restriction prevents a Linux floating-point approximation from silently changing gameplay.
`original_damage_check.py` verifies 28 vectors against original x86 instructions.
The vectors use TankOGas's real CellSpread, PercentAtMax, and one armor verse.

`select_resolved_table_target()` ports the global-table maximum reduction in 006F8DF0.
The caller must supply candidates in original scan order with resolved eligibility and scores.
It keeps the first maximum and excludes scores at or below -1.
`threat_categories()` ports the original threat-mask conversion.
These functions do not replace 006F7CA0's eligibility and scoring logic.

`target.h` ports the first decision stage of 006F7CA0 through 006F7E24.
Inputs use typed optional fields where the full object model is not yet available.
The result distinguishes Reject, Continue, and NeedsInput.
NeedsInput identifies the exact original address or virtual slot that needs data.
Continue only passes this stage. It does not establish complete eligibility.
The stage covers fire errors, armor threshold, cell category, health, concealed ownership, mission control, and height.
Fifteen decisions match original x86 execution with explicit virtual/map callback outputs.
Run `original_target_check.py` to reproduce this evidence check with Unicorn.

Damage arithmetic uses the recovered 00000E7F floating-point control mode.
This selects 53-bit arithmetic and truncation toward zero, including single-precision stores.
Linux's default nearest rounding changed several test results, so the port applies truncation explicitly.
The code also uses a fused multiply residual to resolve armor multiplication rounding.
The original damage evidence check starts with this control mode explicitly enabled.

## Threat score recovery

`threat_score()` ports 0070CD10 using resolved type, weapon, house, and distance inputs.
It preserves the original signed retaliation term, armor effectiveness, special threat, health ratio, and preferred enemy bonus.
It adds the original 100000 score bias from 007F4E90.
The score uses current health divided by Strength through 005F5C60.
It uses complete-object type fields +2C8, +2D0, +2D8, +2E0, and +2E8 for its five coefficients.
These correspond to MyEffectiveness, TargetEffectiveness, TargetSpecialThreat, TargetStrength, and TargetDistance coefficients.
The caller resolves the normal or dumb coefficient set before calling.

The original explicit-origin branch preserves distance in world units.
The default-origin branch divides resolved distance by 256.
Both branches subtract range divided by 256 and clamp the difference to zero.
The port preserves this unusual unit difference instead of replacing it with a new distance model.
Five score cases match original x86 arithmetic with fixed virtual outputs and fixed square-root outputs.
`original_score_check.py` reproduces those checks.
The original square-root helper 004CAC40 uses a lookup table, so this API accepts its resolved integer output.
The port does not substitute the standard library square root for the original lookup table.

`fp.h` applies the original 53-bit truncation mode to score operations without changing the process rounding mode.
It uses exact residuals for addition, multiplication, and division within a bounded normal numeric domain.
Its host arithmetic assumes the ordinary nearest-rounding environment and excludes fast-math builds.
Nonfinite, extreme, and subnormal-domain inputs return explicit errors.

## Base RA2 checks

The separate base export now supplies evidence under `rebuild/decomp-base/`.
Its indexes live under `analysis/base-ra2/indexes/`.
The following equivalences were checked against base bodies and original `game.exe` instructions:

| Behavior | Base RA2 | Yuri's Revenge |
| --- | --- | --- |
| Random seed, draw, range | 00638790 / 00638840 / 006388A0 | 0065C6D0 / 0065C780 / 0065C7E0 |
| Armor/range damage | 0047E1C0 | 00489180 |
| Guarded drive destination | 0049FE30 | 004AFD40 |
| Ground path processing | 004A2670 | 004B2630 |
| Ground curve transform | 004A4740 | 004B4780 |
| Lookup distance helper | 004BA410 | 004CAC40 |
| Lookup distance table | 00817DFC | 008650BC |
| Floating-point integer conversion | 0077F110 | 007C5F00 |
| Floating-point control word | 007D8964: 00000E7F | 00822D80: 00000E7F |

Base x86 checks reproduce the same 15 random draws, 28 damage vectors, and 64 curve vectors.
Both random constant arrays match exactly.
All 16,384 distance table words match exactly.
These are specific shared primitives, not a claim that all engine behavior matches.

Ground destination guards use base virtual offsets +324, +328, +1BC, and +1C0.
Their expansion counterparts are +37C, +380, +1D4, and +1D8.
`GroundGuards` retains the expansion field labels, but inputs represent the four ordered predicate results.
The base Foot path array starts at +4D0 instead of expansion +5E0.
The typed native state does not read either original memory layout directly.

The three RNG, ground, and damage evidence scripts accept `--base` with a `game.exe` path.
Base target differences and base score checks are documented below.
Current map/rule fixtures are original expansion data, because no base RULES.INI is present in the supplied extracted configuration directory.

`OriginalDistance::from_executable()` recognizes both verified helper signatures.
It reads the appropriate table from either original executable without executing it.
Its typed coordinate method preserves the original lookup approximation, including its integer truncation.
The native tests also compare nine lookup outputs by their exact float bits.

## Unknown map types

The original object readers skip missing type-registry entries before allocating an object.
Evidence includes expansion 00743270, 0051FB00, 0041B110, and 0044F820, plus base unit reader 00706AE0.
The native loader now follows this branch and reports each skipped object in `snapshot().skipped_objects`.
A type registered under another category does not satisfy the requested category.
Case-insensitive type ID matching follows the original registry lookup behavior.

A missing rule section does not prove a missing type.
For example, original BuildingTypes declares CALA02 and CALOND02 even though their sections are commented out.
The native registry keeps those declared types and leaves unresolved fields unknown.
It does not guess their original constructor defaults.

Run `rebuild/src/sim/test.sh /path/to/user/ra2/analysis/extracted --all-maps` for the full map audit.
Each map checks that imported and explicitly skipped records account for all original object rows.
The audit prints skipped type IDs with reasons.

## Base target differences

The prefix defaults to base RA2. Set `CandidateEdition::YuriRevenge` explicitly for the expansion path.
Base 006C560D–006C5622 multiplies the armor verse by double 100, truncates, and rejects exactly one.
Expansion 006F7D3D–006F7D4F compares against float 0.02 and rejects lower, equal, and unordered values.
The base prefix does not contain the expansion's +604 cell gate.
Optional field labels retain expansion offsets, while diagnostics identify the selected edition's actual address and field.
Base exceptional armor conversion remains an explicit unresolved case.
Seventeen base prefix decisions match original x86 instructions with resolved callback inputs.
Base score function 006D7550 also reproduces the five score vectors from expansion 0070CD10.

Base factory update 004B9300 uses the same verified cash-only stage and payment decisions as expansion 004C9B20.
The update pointer appears in base factory vtable 007A0EE0 at slot +5C.
Base interval calculation is 004B9780, and its timer helper is 00453EB0.
Base cash debit 004E5280 has the corresponding cash-first branch at house +24C.
The expansion stores that cash at house +30C.
`original_factory_check.py` executes both original factory updates with a resolved cash-only adapter.
Both editions produce replay hash 72681CC7 across all 108 test frames.
Both stall without money and resume with money.
The native test checks the same per-frame progress, remaining cost, cash, and spent hash.

## Terrain injection and floor height

The optional fourth `Simulation` constructor argument accepts `TerrainCells` from `world.h`.
Inputs are copied, and duplicate cell coordinates fail explicitly.
The raw TMP terrain byte and the final cell LandType are separate fields.
The decoder may leave final LandType empty until overlay and object changes are known.
A known floor height makes supported ground vehicle positions available in original world units.
Infantry subcell placement, building placement, aircraft placement, and bridge placement remain optional unknown positions.
Unknown floor height leaves that optional position empty.

`floor.h` exposes the original floor formula for signed height levels and all 20 ramp indices.
`original_floor_height(level,ramp,world_x,world_y)` excludes the optional bridge deck.
Original initializers produce LevelHeight 104 and bridge height 416.
The initializers execute unchanged in `original_floor_check.py`.
The oracle uses a PE section loader that leaves uninitialized data zeroed.
All 672 tested level, ramp, and local-coordinate cases match the native formula.
The tests include signed negative levels and the original low-byte coordinate wrapping.

The shared floating-point helpers now support general nonnegative damage radii.
They replace the earlier binary-radius-only implementation.
Both original executables and the native port produce hash AFF8A2E1 for 5,832 armor/range cases.
Nonfinite, overflowing, negative-radius, and negative-distance paths still return explicit errors where applicable.

## Original adjacent Move

Call `load_ground_assets(original_executable_path, original_art_ini)` before issuing commands or advancing time.
Use the matching base or expansion executable and ART INI.
The loader reads original static curve data. It executes no Windows instructions.
It checks the supported distance-helper signature, curve pointer ranges, sample bounds, and control rows.
Base curve descriptors start at 007A00A0. Expansion descriptors start at 007E7A28.
Controls start at base 007A0180 or expansion 007E7B28.

Queue `Command{}` with `op=Op::Move`, the entity ID, an absolute frame, and a destination cell.
A successful issue returns a ticket. The tick result reports whether the specific Move can run.
Movement updates run before due commands. A newly accepted Move starts moving on the following tick.
Read `Entity.position`, `cell`, `moving`, and `move_goal` in snapshots.
The native command API does not reproduce the original network event format.

The command currently supports one full-health, non-veteran, non-accelerating drive vehicle at a time.
Its initial mission must be Guard or Area Guard. Its map tag must be None.
Groups, follow links, bridges, naval movement, and retained unknown fields return explicit errors.
The goal must be one adjacent cell in the vehicle's current eight-way direction.
Both cells must have resolved flat floors at the same height, road land, and no overlay or terrain object.
Clear-cell Move requires ordered tile initialization through the optional TheaterInputs constructor argument.
Foot, Track, and Wheel multipliers must all be one on those cells.
This avoids guessing the unresolved default SpeedType while preserving these equal original rates.
The original country SpeedUnitsMult applies. Its constructor default is float one at 005113F0.

Base 006DCC01–006DCC3B and expansion 0071465F–00714699 clamp authored Speed, scales it by 256/100, and caps it at 255.
004DB1A0 supplies the speed calculation order. Foot construction at 004D31E0 starts the other supported multipliers at one.
Rules loader 00674000 and LandType-name getter 0048DFD0 prove Clear=0 and Road=1.
The supported path chooses only its already aligned adjacent original direction.
The eight offsets come from original initializer 0049F2F0.
This does not substitute a new path finder for 004D3920.

Track updates port expansion 004B0F20 and base 004A1000.
They use original seven-unit sample budgets and original straight curves.
Fractional movement preserves the original float conversion and diagonal third-cell guard.
The terminal branch preserves its Manhattan-distance correction and signed remainder.
The completed segment clears its anchor and selects the recovered idle mission.
Armed vehicles use house IQ, IQ.GuardArea, and DefaultToGuardArea to choose Guard or Area Guard.
House IQ initialization follows 00500B40; IQ rules follow 00674240.
Base 006FD630 and expansion 00738970 have different extra idle-state guards.
`original_idle_check.py executable [--base]` verifies 320 resolved armed idle decisions per edition.
The native checks preserve both hashes: base E4BA2365 and expansion 2A52CF05.
Same-frame segment chaining, replanning, turns, and simultaneous movement return explicit errors.
Static building checks use original ART Foundation names and conservative bounds.
They reject uncertain foundation cells instead of claiming full building collision recovery.
Forced health changes during movement also return an explicit error until damaged-speed and death integration is recovered.

`original_move_trace.py executable [--base]` runs isolated original track updates with explicit flat-map and Foot adapters.
It tests eight directions at speeds 1, 7, 15, 30, and 255.
Both executables produce the same 40 complete traces.
The native tests compare every frame's coordinates, track, sample index, and signed remainder against their hashes.
These checks prove this resolved track slice. They do not prove unrecovered callbacks.

Run `rebuild/src/sim/test_move.sh` for the real-map command check.
This optional integration test reads the parent's current renderer sources through `RA2_SHARED_SOURCE`.
Its default is `/path/to/user/ra2/rebuild/src`.
The shared asset reader uses its normal optimized build. SIM_TEST_FLAGS instruments the simulation and its INI/map dependencies.
It loads original C1A02MD.MAP, the original ART INI, and exact TMP metadata from original MIX archives.
Add `/path/to/user/ra2/game /path/to/user/ra2/analysis/extracted --base` to test base ALL02S.MAP.
The base test reads RULES.INI, ART.INI, and the map from original MIX archives.
The real-map checks now require an explicit error for unknown bridge flags.
They compare actor state before and after rejection, plus deterministic command replay.
Base connected routes also reject unresolved bridge state without changing the actor.
Earlier integration checks inferred clear bridge flags from local terrain and overlay absence.
That inference was wrong. Neighboring bridge roots can set the same flag.
The earlier real-map movement claims are withdrawn until fresh-map bridge setup is proved.
Isolated original drive traces still prove their explicit flat-map input slice.
The two tile passes keep all floor heights unchanged on these two real fixture maps.

## Overlay-free terrain initialization

`world.cpp` ports the raw TMP conversion and the final cliff-back branch.
Base sources are 00529300 and 00472540; expansion sources are 00544BE0 and 0047D2B0.
The raw conversion tables at base 007DE4EC and expansion 008288E4 match.
The six height tests use their exact original offsets and order.
CliffBackImpassability=2 changes eligible land types to rock when a tested neighbor is four levels higher.
Missing required neighbor heights leave the final land unresolved.
Overlay cells remain unresolved until their original overlay decisions are recovered.
Cell construction clears the bridge bit. Later setup can set it from a neighboring bridge root.
Tile and land setup preserve an explicit bridge value, including unknown.
LandType 10 is Tunnel. 00727FD0 creates tube links and writes Cell +116.
Bridge overlay initialization is separate and remains unrecovered.
Base setters 004732D0/00473700 and expansion setters 0047E040/0047E470 write neighboring cells.
Each setter writes bit 100 at the root, two cells forward, and one cell backward.

`original_land_check.py executable [--base]` checks 4,096 original branch cases in each edition.
Both produce hash BC614341. The native tests compare the same grid.
This does not yet port autotile replacement at 0047CA80, bridge creation, tunnel flags, or overlay-specific land decisions.

## Tile replacement decisions

`replacement.h` ports the full tile-index decision at base 00471D10 and expansion 0047CA80.
`TileReplacementRules::from_theater()` binds original General TileSet numbers to their live first indexes.
It preserves disabled groups and the original Lunar overrides from 00545150.
The decision uses four cardinal neighbor indexes and ramp states.
Missing required neighbors return an explicit unresolved decision.
The caller separately loads changed tile metadata through `TheaterInputs::resolved_metadata`.

Run `test_replacement.sh` for 19,084 native decisions.
`original_replacement_check.py executable [--base]` runs the same resolved-input grid in each original executable.
All three produce hash 60DC1EC1.
The grid covers all four terrain borders, extra connected groups, and all 20 ramp indexes.
These numeric indexes are isolated test inputs. They do not assign gameplay values to original maps.
Map-wide integration now follows the original cell order and both scenario initialization passes.

## Ordered map initialization

The optional fifth constructor argument is `const TheaterInputs* theater=nullptr`.
Pass the original theater settings, live TileSet ranges, and static metadata callback.
The callback reports modulo getter values and the separate raw subtile validity flag.
The simulation uses it during construction and does not retain it.

`world_init.h` ports the original map cell iterator and the supported Cell setup slice.
Base 0055A550/0055A490 and expansion 00578350/00578290 use diagonal cell order.
This differs from packed record order.
The first pass starts with constructor ramp values of zero.
It recomputes floor height before loading ground vehicle positions.
The second pass runs after map objects, matching 00686B20 and 00568BB0.
The simulation performs exactly two passes. It does not iterate until tile choices stop changing.

Cell setup retains the old TileType receiver after a tile index changes.
The new tile supplies terrain on the next pass.
Invalid non-overlay subtiles become clear cells with index 0xFFFF and subtile zero.
An existing 0xFFFF sentinel preserves its authored subtile and skips metadata lookup.
Out-of-range positive live indexes become 0xFFFF.
Missing static metadata and unsupported overlay-specific invalid subtiles produce explicit errors.

`TerrainCell.tile_index` keeps the original packed value.
`resolved_tile_index` keeps the live value after translation and replacement.
`tile_replacement_resolved` marks cells processed by the original ordered setup.
Clear-cell Move requires this flag. Road Move keeps its earlier supported input path.
`terrain()` returns the final read-only cells for the native renderer.
The renderer must not translate resolved indexes again.

`original_cell_order_check.py executable [--base]` checks five iterator sizes, including three real map sizes.
`original_world_check.py executable [--base]` executes the original Cell bodies and metadata getters across two passes.
It checks six complete snapshots with resolved non-overlay test data.
These include ramp neighbors, changed terrain, cliff height, invalid indexes, invalid subtiles, and sentinels.
`test_world_init.sh` compares the native iterator and all six snapshots.
Both original editions agree with the native output.

Overlay land decisions, bridge/tube objects, map animations, shadow objects, and the passability cache remain gaps.
Relevant original functions include 0047E040/0047E470, 00727FD0, 00421EA0, 00547370, and 00483C80.
The Cell index, ramp, floor, and supported non-overlay land decisions do not replace those effects.

## Sparse packed terrain

Original map allocation creates the full diamond before packed terrain records load.
Base Cell constructor 00470F10 and expansion 0047BBF0 share the tested defaults.
`original_default_terrain_cell(cell)` exposes tile 0xFFFF, subtile zero, level zero, ramp zero, clear land, and no bridge.
The scenario's 00653F50 call supplies initial height zero to 00565C10.
A missing packed record keeps those defaults.
Overlay loading still applies the separate full overlay grid to that cell.
The adapter must supply all allocated cells, including omitted packed records.
`original_defaults_check.py executable [--base]` executes both original constructors and verifies the default fields.

## Production timing

`original_build_time` ports base `006C22A0` and expansion `006F47A0`.
It uses original cost timing, power penalties, factory discounts, and wall timing.
Base RA2 does not apply the expansion's `BuildTimeMultiplier` step.
The caller supplies the resolved country multiplier and factory count.
The function rejects values outside its verified numeric range.
Prerequisites, production commands, complete queues, and placement remain incomplete.

The original executable check covers 2,052 cases per edition.
Five native checks also connect these times to the cash-only factory timer.

```sh
make -C rebuild sim-build-time
PYTHONPATH=/tmp/ra2-sim-capstone /usr/bin/python3 rebuild/src/sim/original_build_time_check.py game rebuild/build/sim_build_time_test
```

## Connected original drive paths

`Command.path_directions` can supply an already resolved original path of 1..23 directions.
Direction zero is north. The other seven directions follow clockwise.
An empty vector keeps the adjacent-goal API.
This input does not claim to recover the original path search.

`DriveSegment` ports the clear-cell preparation branch from base 004A2670 and expansion 004B2630.
The shared 64-row control table chooses original curves 1..6.
A turn can consume two path entries and use their final cell as its curve anchor.
The native state keeps the original signed remainder, curve sample, direction queue, and facing writes.
Base 004A05F0 and expansion 004B0500 control same-frame continuation.
A newly prepared track has different continuation rules from an already active track.
The last turn can be 45 or 90 degrees in either direction.
Straight steps can precede that final turn.
Mid-curve track connections and initial turn-in-place remain explicit errors.

Move validates every conservative swept cell from original samples and interpolation bounds.
A turn can cross cells outside the direction queue.
Each crossed cell must have supported flat terrain and no intersecting object bounds.
The command validates its complete route before it changes the entity.
Path search, changed routes, simultaneous movers, ramps, bridges, and dynamic collision responses remain gaps.
Original search functions include 00429A90, 0042C900, 0042D170, and the Foot wrapper 004D3920.

`original_route_check.py executable [--base] --vectors` executes the original wrapper and its preparation and follow bodies.
Its adapters supply resolved empty cells, full health, fixed speed, and a direction queue.
Original facing and static direction initializers execute as x86.
The native game reads only the original static curve data.

`test_route.sh` compares 440 complete original traces in each edition and ten explicit limit checks.
The traces include eight headings, five speeds, 2/3/23-cell straight paths, and both final turn sizes and signs.
They compare every frame's position, facing, track, sample, signed remainder, and all 24 path entries.
Both x86 editions produce identical vectors.
Captured base HTNK 61 route vectors remain in `real_route_golden.inc` for later setup recovery.
Their resolved drive traces take 23, 26, and 19 ticks for {4,4}, {4,3}, and {4,2}.
The current real-map test rejects these commands because bridge state is unknown.
It does not supply assumed clear flags to restore the earlier movement claims.

## Setup random-state handoff

`Random::state()` exports all 250 words and both indexes as `RandomState`.
`Random(RandomState)` restores the original stream without drawing or reseeding it.
It checks the original index bounds and the fixed 103-entry separation.
`Simulation::load_random_state(state)` accepts this value before ticks or queued commands.
This preserves draws already consumed by original skirmish setup.
The tests resume original x86 golden sequences and continue the setup stream through a queued command.
Invalid indexes and state replacement after command issue or ticks produce explicit errors.

## Separate hull and turret facings

`Entity.facing` follows the drive-written hull facing in 0..255 units.
`Entity.turret_facing` stores an independent optional value in the same units.
Base placement 006FC95E and expansion 00737BCC initialize the turret from the original map byte.
The hull matrix at base 0053D04A reads the drive-facing field at actor +2E0.
The separate turret field is actor +2F8, or +3A0 in the expansion.
Straight supported movement preserves the initial turret value.
A command with a turn clears the value before movement starts.
Original Unit AI turret updates remain unrecovered. The renderer must keep an unknown turret explicit.
Other entity kinds have no recovered turret initialization here.

## Regular goal-path search

`find_original_flat_path()` ports base 00427370 and expansion 00429A90 with their original node creation helpers.
The input is a resolved flat search graph. It does not infer UnitClass eligibility from map artwork.
The flat wrapper supplies equal Cell height levels. Both entry points exclude bridges, tubes, marked terrain, and hierarchical search.
The ordinary Unit profile has type +C94 and +E0C clear, with the constructor cost weight of one.
A missing graph record means an unallocated original cell. An empty entry status means an unresolved callback.
This slice accepts only original entry results zero and seven.
Other entry results need occupant-dependent costs from 00429830 and return Unsupported.

The search keeps the original eight-direction order, float stores, lookup square root, and open-list tie behavior.
It marks cells when it creates nodes. It does not replace those records with a generic decrease-key search.
It preserves the separate last-child choice before it compares the open-list root.
Blocked goals can return a partial path that stops short of the requested cell.
The result distinguishes Found, Partial, NoPath, NeedsInput, and Unsupported.
The native command layer does not yet call this search. The original Foot wrapper and cell eligibility remain required.

Shortest monotone paths keep their original reconstructed direction list.
Paths that reverse an axis or use extra steps need explicit PathPostprocessing state.
The recovered full sequence then preserves original corner checks, reversal windows, and cleanup.
The original default expansion limit is 0xFFF7. The original 10,000-expansion failure case is also preserved.
The original 65,536-node storage bound produces an explicit native error before overflow.

`original_path_check.py executable [--base] --vectors` executes the original search, node creation, and path postprocessing.
It supplies a resolved flat 9x9 test graph through the CanEnterCell callback.
It tests all 81 flat goals, a blocked goal, an impassable row, and an alternate monotone route.
Four more cases cover ramp bytes, blocked height steps, a descent and return, and an unreachable level change.
Both original editions return identical paths and node-cost float bits.
`test_path.sh` compares those 88 cases in each edition and eight explicit input/limit checks.
These 184 checks also verify unknown eligibility and unported nonmonotone postprocessing.

## Cell height-entry gate

`original_height_entry()` ports base 004C8730 and expansion 004D9C60.
It keeps destination/source signed levels, ramp bytes, and independent Cell flag bits 0x100 and 0x200.
It returns the original zero/seven decision and both mutated outputs: layer height and bridge selection.
A blocked decision can still update the layer-height output, matching the original branch order.
The direction -1 query has its separate bridge-level behavior.
A missing required source lookup and signed-difference overflow produce explicit errors.

This function proves only the height gate. It does not prove full UnitClass eligibility or enable ramp movement.
`original_entry_height_check.py executable [--base]` executes 112,896 cases in each original executable.
Both return hash 5D4CC105.
`test_entry_height.sh` checks the same complete native grid and explicit missing-input/overflow errors.

## Ground search height profiles and original Unit entry

`find_original_ground_path()` adds resolved Cell height profiles to the regular search.
It calls the recovered height gate on each directed edge and preserves the blocked-goal height test.
The heuristic still uses X/Y only, matching 0042A460. Selected physical drive tracks need separate floor checks.
The flat API remains available as a zero-height wrapper.
Unknown height inputs and bridge path states produce explicit errors.

The path oracle now executes the complete original Unit entry at base 00703040 and expansion 0073F0A0.
Only designated test walls supply an external blocked result. Empty cells execute the real Unit, height, tube, and locomotor bodies.
`original_empty_entry_check.py executable [--base]` separately verifies 4,608 original empty-cell decisions per edition.
It varies restricted land, land category, heading, terrain speed, and same-level ramp profiles.
Both editions return hash 28ABC0F5.
Map bounds in this oracle are explicit adapters. It tests both bounds answers and both Unit on-map flag states.
Hierarchy still needs integration.

`SimDecompileAt.java` recovers missing function entries into a transient Ghidra view and prints evidence to stdout.
Run it only with `analyzeHeadless -readOnly -noanalysis`.
Both Unit entry bodies were recovered this way. Ghidra reports that it discards the transient changes.
No decompiled source files or naming indexes are changed.

Ordinary far-goal Move stays unsupported until 0042C900/0042C290 hierarchy and occupied-cell decisions are integrated.
Unit map placement sets +3D5, which can enable that hierarchy branch.
The command layer does not silently substitute the regular-search branch.
Supplied original paths continue to execute through the tested drive state.

## Original maximum building bounds

Original maximum foundation height adds one row only when Rules Bib=yes.
Base 00458940 and expansion 0045ECA0 make this decision. Their constructors initialize Bib to false.
Width comes from base 00458930 and expansion 0045EC90.
The Move guard now uses this proven maximum instead of adding a row to every building.
`original_foundation_check.py executable [--base]` verifies all 22 foundation sizes, both Bib states, and both include-Bib arguments.
Both editions pass 88 cases with hash 7737195D.
Move also rejects IsTrain=yes and authored Tubes sections until their extra state is recovered.

## Initial coarse map regions

`build_original_coarse_regions()` ports base 0054F250 and expansion 0056CB90.
It preserves the row scan in 0056C510 before tube edges and movement-zone reachability tables.
Inputs are the original Cell +4C cached category and raw unsigned height byte.
The cached category is not LandType. The caller must recover that cache before using real terrain.
Unknown input, a missing blocked border, and native storage limits produce explicit errors.

The scan retains the original asymmetric height checks: left steps allow one; right steps allow three.
The upper and lower fringe scans include both diagonal endpoints.
Category six relaxes height checks for region links, but does not relax component filling.
The result includes cell labels, original reported sizes, largest-region identity, and ordered region pairs.
Edge order follows the original 256 buckets and insertion order within each bucket.
This is a graph-building primitive. It does not yet enable ordinary far-goal Move.

`original_region_check.py executable [--base] --vectors` executes the original recursive fill without replacing its decisions.
Only the driver and preallocated vector storage are supplied by the test.
Both editions match all 256 cases, including flat regions, alternating categories, height changes, and category-six links.
`test_regions.sh` compares complete result hashes and verifies seven explicit input cases: 263 checks.
The same native checks pass with address and undefined-behavior sanitizers.
Tube edges, final Cell category recovery, three hierarchy layers, and hierarchy search remain separate work.

## Three map hierarchy layers

`build_original_hierarchy()` ports base 00563D80/00564290 and expansion 00581F90/005824A0.
It builds levels two, one, and zero in that order, with the original eight-, four-, and two-cell block boundaries.
Each cell supplies a resolved coarse category, raw height byte, and original usable-area answer.
It retains raw component boundaries, height tests, parent identities, center indices, and ordered neighbor lists.
Each link retains its original diagonal flag. Repeated discovery keeps the first flag instead of combining flags.
Dummy node zero has no recovered center value, so its native center stays unknown.
Tube and bridge transition records are excluded from this slice.
Missing required inputs, invalid blocked borders, and native storage limits produce explicit errors.

`original_hierarchy_check.py` executes the original recursive fill with an explicit test driver.
`original_hierarchy_builder_check.py` additionally executes the complete original builder and node-list construction.
The complete oracle supplies usable-area results and memory allocation. It preserves original gameplay decisions.
Both original editions return the same 256 complete three-layer results in both oracles.
The test includes flat regions, alternating categories, varying heights, and selectively unusable boundary cells.
`test_hierarchy.sh` checks all 768 native layer hashes and one unresolved-input error.
All 769 checks also pass with address and undefined-behavior sanitizers.
Hierarchy search and the Cell +4C producer still need integration before ordinary far-goal Move works.

## Cell coarse category producer

`original_coarse_category()` ports base 00478F40 and expansion 00483C80, which write Cell +4C.
Its result is the input category for map regions. It is not LandType or full Unit entry eligibility.
Inputs preserve the usable-area answer, overlay bytes, final LandType, Wheel table floats, and ordered ground occupants.
The Wheel field corrects an earlier Track label. Unit movement speed remains a separate input.
Base reader 0064D610 writes Track at 00850EA4 and Wheel at 00850EA8, with a 36-byte land stride.
Expansion reader 00674000 writes Track at 0089EA44 and Wheel at 0089EA48.
Category functions read Wheel at 00850EA8 / 0089EA48 for both overlay and final Cell land.
`CoarseOverlayState.land_wheel` and `CoarseCategoryInputs.land_wheel` require that original column.
`original_land_category_check.py executable [--base]` executes the actual reader, then the actual category function.
Its supplied INI answers use eight distinct Track and Wheel pairs, including opposite zero and nonzero values.
Original table writes, clamps, float storage, and category reads execute unchanged.
Both editions match hash 8D749C93.
The native category test compares those vectors and checks real Tiberium Track=70% versus Wheel=50%.
Building, owner, terrain-type, and Session fields keep explicit offset labels until their meanings are proved.
The header lists both editions' offsets. Unknown required fields produce errors at the original point of use.
Early returns do not require fields that the original branch does not read.

The result retains every ordered cache write, including building writes of six that a later zero overwrites.
It preserves the original overlay zero comparison, the double 0.01 terrain threshold, and unordered float branches.
Only runtime-type virtual answers and usable-area answers are adapted by the original x86 oracle.
`original_category_check.py executable [--base]` matches 4,096 cases per edition with hash DA970EA0.
`test_category.sh` passes 4,115 checks, including five terrain entries from original rulesmd.ini and required-input errors.
The same checks pass with address and undefined-behavior sanitizers.
The producer does not recover overlay constructors, occupant placement order, or the fields it requires from those objects.
The terrain adapter must supply the resolved original inputs, or keep that cell category unknown.

## Hierarchy route search

`find_original_hierarchy_route()` ports base 00429B60 and expansion 0042C290 with zero actor-threat contribution.
It searches levels two through zero using the original ordered graph, float costs, heap ties, and parent-region filter.
Inputs include explicit edition, endpoint cell indices, movement-zone value, and blocked edges for each level.
The result retains each selected region route, allowed-region marks, and optional best-cost floats.
A failed level preserves the earlier levels' completed state, matching the original search.
This is the hierarchy primitive. It does not run the Foot goal wrapper or choose new blocked edges after drive failures.

The compatibility table is at base 007E00C4 and expansion 0082A594.
The first twelve rows match. Expansion adds a thirteenth row.
Base INI reader 0046A300 accepts twelve MovementZone values; expansion 00474E40 accepts thirteen.
Category costs match at base 0079C4A4 and expansion 007E3794.
The original double diagonal cost is 0.001 at base 0079C528 and expansion 007E3818.
Native arithmetic preserves the original E7F rounding and float stores.
Invalid graph inputs, unknown editions, unsupported zone values, and original storage bounds produce explicit errors.

`original_hierarchy_route_check.py executable [--base] --vectors` executes the original full hierarchy builder and route search.
It supplies zero actor-threat contribution and selected blocked edges through the original input state.
It checks four endpoint pairs and every valid movement-zone row across 256 resolved graphs.
Base executes 12,288 searches. Expansion executes 13,312 searches.
`test_hierarchy_route.sh` checks all 25,600 routes, allowed marks, generated-node cost bits, and six invalid-input cases.
The same 25,606 checks pass with address and undefined-behavior sanitizers.
Threat weighting, Foot wrapper retries, regular-search hierarchy filtering, and live-world inputs remain separate recovery work.


## Regular-search hierarchy filtering

`find_original_ground_path()` accepts an optional `PathHierarchyFilter` with the original allowed-zone marks.
Each ground record then supplies its optional level-zero hierarchy zone and Cell +122 byte.
Base 00427370 and expansion 00429A90 skip outside-route cells only when that byte is zero.
A nonzero byte can admit such a cell. It does not prove the later Unit entry decision succeeds.
The native code tests the zone before it tests the entry state, matching the original branch order.
An unused neighbor byte can remain unknown when the zone is allowed.
Unknown required zones, marks, or neighbor bytes return NeedsInput.

`original_filtered_path_check.py executable [--base] --vectors` executes thirty original cases per edition.
It supplies selected zone marks and zero, one, or 255 in Cell +122.
Both editions return identical paths and search cost bits.
`test_filtered_path.sh` passes 140 checks, also with address and undefined-behavior sanitizers.
It tests each fixture with and without explicit smoothing state, plus missing-state and unsupported-domain cases.
The existing regular-path checks still pass all 184 cases. Their original 88 vectors per edition are unchanged.

The new fixture proves monotone paths can need smoothing too.
For a restricted zone with a permitted endpoint row, the original shortens directions north, northeast, east to two northeast steps.
Its reported search cost still describes the three unsmoothed steps.
Without PathPostprocessing, the native API rejects extra-step monotone paths.
With resolved zero actor threat weight and required cell state, it runs the recovered smoothing checks.
It does not return an unchecked unsmoothed path as a verified original route.

## Monotone path smoothing

`path_smoothing.h` exposes the recovered monotone slice of base 004290C0 and expansion 0042B7F0.
It includes the backward window finder at 00429570/0042BCA0 and replacement checks at 004296F0/0042BE20.
The caller supplies raw directions, the original per-step layer inputs, explicit zero actor threat weight, and a read-only cell probe.
The probe supplies resolved Unit entry results, signed cell level, and independent Cell flags 0x100 and 0x40000.
Unknown fields remain optional and fail only when the original branch needs them.

The code preserves both candidate orders, early blocked probes, bridge-level updates, and deleted raw slots.
It processes only the first twenty raw slots, then compacts the full path as the original does.
Nonmonotone paths, tubes, nonzero threat weight, and missing required state stay explicit.
`original_smoothing_check.py executable [--base] --vectors` executes the original whole smoothing pass.
It supplies resolved virtual-entry answers and cell lookups, and records every ordered query.
Both editions match all 256 paths, returned lengths, query orders, headings, and layer values.
The fixtures cover all four monotone quadrants, blocked cells, marked cells, bridge flags, changing levels, and paths beyond twenty slots.
`test_smoothing.sh` passes 266 checks, also with address and undefined-behavior sanitizers.
Regular search enables this primitive only when the caller supplies its required actor and cell state.


## Smoothing integration and original Unit guard

`PathPostprocessing` provides an optional resolved actor threat weight to `find_original_ground_path()`.
The supported smoothing profile requires an explicit zero.
Its ground records also supply optional Cell flag 0x40000.
Unknown required marks, shortcut cells, or levels return NeedsInput. Nonzero threat and shortcut bridge states remain unsupported.
The integrated path retains the original search cost after postprocessing changes its directions.
Marked shortcut cells can reject a replacement and keep the original longer route.
An explored marked cell still needs its unported regular-search cost and returns Unsupported.
Callers that omit PathPostprocessing keep the existing shortest-monotone profile.
Link `path_smoothing.cpp` with `path.cpp`.

The Unit-entry oracles now execute base 004C8CA0 and expansion 004DA1D0 at virtual slots +2F4 and +320.
They no longer replace that guard with a constant result.
Both editions preserve all 4,608 empty-entry decisions, hash 28ABC0F5, and all 88 earlier regular-path vectors.
The expanded filtered oracle checks ordinary shortcuts and marked shortcut rejection through the real Unit entry chain.
The core test still passes 2,511 checks. Live-world hierarchy binding and the Foot retry wrapper remain unimplemented.

## Full no-tube path postprocessing

`smooth_original_path()` adds the complete two-pass sequence with resolved zero actor threat weight.
The first pass is base 00428AE0 / expansion 0042B210, with corner helper 00428CF0 / 0042B420.
The second pass keeps the original reversal state, backward windows, first-twenty-slot limit, and final cleanup.
It preserves direction changes, deleted slots, and both candidate orders.
The earlier monotone API keeps its narrower input contract.

The corner helper reads one cell ahead after each entry check, including a blocked or final check.
Its next entry can therefore use the next cell's level rather than the preceding cell's level.
The API uses a separate read-only metadata lookup to preserve that order.
An entry-only callback cannot substitute for this lookup.
The native result keeps the input directions unchanged when required state is unknown.
Tubes, nonzero threat contribution, and lookups outside the verified 512-square grid remain explicit errors.

`original_smoothing_check.py executable [--base] --full --vectors` runs both original passes and all their helpers.
Its original house threat table is initialized to zero. Original threat lookups still execute.
Both editions match all 256 complete direction and entry-query hashes.
The fixtures include reversals, repeated corners, closed paths, blocked entries, marked cells, and changing levels.
`test_full_smoothing.sh` passes 266 checks, including required lookahead after a blocked entry.
These checks pass with address and undefined-behavior sanitizers.
The original 256 monotone vectors and 266 native monotone checks remain unchanged.
Regular search enables the complete sequence when PathPostprocessing and required cell state are present.


## Complete regular-search postprocessing integration

`find_original_ground_path()` now runs both recovered postprocessing passes for nonmonotone and extra-step paths.
It requires explicit PathPostprocessing state and preserves the separate entry and metadata callbacks.
The regular search still rejects unrecovered bridge, tube, marked-cost, and other occupied-cell cost states.
A forbidden hierarchy zone can force a detour that original postprocessing later shortens through a valid entry cell.
The entry checks retain their original order. They do not reapply the search's hierarchy filter.
A missing lookahead cell produces NeedsInput even after the final candidate entry succeeds.

`original_postprocessed_path_check.py executable [--base] --vectors` tests seven obstacle patterns and twenty-five goals.
Both editions match all 175 complete paths, blocked-goal stops, and original search cost bits.
`test_postprocessed_path.sh` passes 350 checks, also under address and undefined-behavior sanitizers.
The expanded filtered test passes 140 checks, including the successful corner cut through an excluded hierarchy zone.
Its missing-lookahead test preserves the distinction between entry dispatch and metadata lookup.
All 184 earlier native path checks and 88 original vectors per edition remain unchanged.
The original house threat table is supplied as resolved zero data. Original corner threat lookups still execute in the oracle.
Live-world goal commands still need staged state and the original Foot goal-wrapper retry logic.

## Movement-component tables and ground lookup

`build_original_movement_regions()` ports the final graph/table phase of base 0054EBD0 and expansion 0056C510.
It consumes original ordered coarse-region pairs and preserves the neighbor-list order.
Each movement-zone row uses the original eligibility table, stack traversal, ascending seeds, and word-sized group labels.
Label one records an ineligible region. Labels two and above identify eligible connected groups.
The original dummy region zero receives 0xFFFF after each row is built.
The caller still runs the remaining original goal and entry checks.
Tube and bridge edges must already be resolved before a caller supplies them to this phase.

`original_ground_component()` ports the ground-only tail of base 0054F8F0 and expansion 0056D230.
It uses signed X/Y, the original width+height+1 stride, linear aliases, and index clamping.
Its result reads the selected movement row through the raw region label.
The above-bridge selection branch remains separate.
`movement_zones.h` holds the shared original compatibility table used by both component and hierarchy searches.

`original_movement_regions_check.py executable [--base] --vectors` executes the complete original coarse builder, component builder, and ground lookup.
Its adapters supply memory allocation and fixed edge storage. Original graph decisions execute unchanged.
It matches 256 complete graph/table/query hashes per edition.
`test_movement_regions.sh` passes 512 graph hashes, 89,600 ground-query inputs, and ten invalid-state checks.
The same checks pass with address and undefined-behavior sanitizers.
All 25,606 existing hierarchy-route checks still pass after the compatibility table is shared.
The Foot goal wrapper remains the next integration dependency.

## Ground goal-wrapper control

`run_original_ground_goal()` ports base 0042A1D0 and expansion 0042C900 after resolved map/type queries for an ordinary Unit.
Its checked profile uses search mark mode zero. Other mark modes still need 004285D0/0042ACF0 state recovery.
The inputs retain both movement components and the lazy hierarchy guards.
Unknown required fields return NeedsInput. Guards that the original skips can remain unknown.
Callbacks operate on private search state and supply hierarchy, regular-search, reset, and retry results.

The control preserves the initial reset, three blocked-list clears, hierarchy fallback, and retry ordering.
Only expansion limit -1 permits five attempts. Every other value permits one, including other negative values.
A failed final regular attempt still runs retry and reset before the limit check.
Only the initial hierarchy failure falls back to regular search. A later hierarchy failure ends the wrapper.
The result records callback order and the final Search +38 permission flag.
Native Unsupported and NeedsInput results stop immediately instead of becoming a false no-path answer.

`original_goal_control_check.py executable [--base] --vectors` executes the original wrapper branches.
Map/type queries and search/retry answers are explicit adapters in this control test.
Both editions match 512 complete callback traces, permission states, and returned success flags.
`test_goal_control.sh` passes 527 checks, including unresolved callbacks and mark-mode errors.
The same checks pass with address and undefined-behavior sanitizers.
The native hierarchy and regular-search primitives still need a checked world adapter for this controller.
Retry decision bodies at 0042A590/0042CCD0 and their local reachability helpers remain separate recovery work.

## Ordered Overlay placement dispatch

`overlay_mark.h` ports the resolved Overlay mark dispatcher at base 005DA970 and expansion 005FC570.
These missing entries were recovered with `SimDecompileAt.java` in read-only Ghidra projects.
`dispatch_original_overlay_mark(mode, environment, callbacks)` returns the original boolean outcome or an explicit gap.
Its callbacks must resolve the current actor type, root cell, existing overlay flag, and each reached effect.
Empty fields and missing effect bodies return NeedsInput. They do not mean false or success.
Effects execute in order. An unresolved later effect does not undo earlier effects.
Use a private staged world before publishing changes.

The dispatcher calls parent Object mark before checking modes 1 and 3.
It rejects ramps above four except for original type index 178.
Bridge roots 24, 25, 237, and 238 call their setters before land, wall, or clearance checks.
The branch preserves veinhole neighbor order, crate dispatch, land-nine writes, and wall failure disposal.
Walls update map zones, the optional import frame, and eight neighbor counters before final setup.
Ordinary placement preserves the three clearance argument sets and the existing-overlay blocking check.
Resource and type-byte writes retain their original order.
A failed clearance can still finish and return true without writing an overlay.
The code rechecks the placement global after clearance and the type byte after resource updates.
Final setup runs before the object flag writes and disposal.

Bridge approach indexes 122..125 and 233..236 return Unsupported.
Their geometry and shared RNG writes remain separate recovery work.
CreateAnimation delegates the complete original branch to the caller.
No animation, bridge setter, parent mark, placement query, or setup body is invented by this dispatcher.
This API does not prove fresh-map bridge flags or enable live movement.

Important called bodies, base / expansion:

- Parent mark: 005D4160 / 005F5850.
- Bridge setters: 004732D0 and 00473700 / 0047E040 and 0047E470.
- Veinhole query: 00710A60 / 0074D670.
- Crate creation: 0047AD00 / 00485AF0.
- Wall query and update: 004718B0 and 00475960 / 0047C620 and 00480630.
- Zone updates: 0054FC60 and 00566340 / 0056D5A0 and 00584550.
- Neighbor lookup: 00476AC0 / 00481810.
- Clearance: 00478760 / 004834A0, with final arguments (-1,0,-1,1).
- Resource update: 00476BA0 / 004818E0.
- Cell setup: 00472540 / 0047D2B0, with argument -1.

`original_overlay_mark_check.py executable [--base] --vectors` runs 4,096 original cases per edition.
Compare every line with `test_overlay_mark.sh --vectors`.
Both editions and native code preserve aggregate hash 6D535F32.
The vectors compare ordered effects, direct cell writes, neighbor byte wrap, object flags, and return values.
Callbacks change the placement global and resource type byte in selected cases.
Allocation returns null. The animation path then uses a supplied no-owner query.
The test supplies called bodies and map lookup. It proves dispatcher control, not those called bodies.
`test_overlay_mark.sh` also checks eight unsupported approach indexes and two missing-input cases.

## Hierarchy retry edge selection

`block_original_hierarchy_exit` ports base 0042A840 and expansion 0042CF80.
It consumes one resolved hierarchy layer, its original path, the current region, and existing retry state.
It selects the first matching path entry. At the last entry, it selects the preceding edge.
It appends that edge and matching common-neighbor edges in the original reverse link order.
It preserves duplicate edges and existing insertion order.
Short paths and absent regions clear Search +38. Successful selection preserves its previous value.
Unknown endpoint adjacency returns NeedsInput without changing the supplied state.
Paths beyond the original 500-word buffer return Unsupported.
The ordinary vector append executes at base 0042B0F0 and expansion 0042D830.

`original_retry_edges_check.py executable [--base] --vectors` runs both original bodies without stubs.
Compare its 4,096 lines per edition with `test_retry_edges.sh --vectors`.
Both editions and native code preserve aggregate hash 0BBA8654.
`test_retry_edges.sh` also checks duplicate order, unknown adjacency, path limits, and empty-path state.
The tests supply initialized vector storage with enough capacity. Allocation failure is outside this slice.
Local reachability at base 00565EB0 / expansion 005840C0 remains separate.
The outer retry dispatcher and persistent regular-search progress must be resolved before live goal commands use this helper.

## Outer hierarchy retry control

`run_original_hierarchy_retry` ports base 0042A590 and expansion 0042CCD0.
It consumes all three paths, existing blocked lists, and the explicit Search +70 progress cell.
It resolves a stable region-entry handle before the first level.
Each level reads that cached entry before the local reachability call.
Unreachable results append reported regions in reverse order, with canonical edge endpoints and preserved duplicates.
Reachable results re-resolve the current progress cell and call the verified retry edge selector.
All three levels run after Search +38 becomes false.
Callbacks can change progress and later region words. The code retains the original read and call order.
The entry handle must preserve the current table identity if a callback can replace the table.

`original_hierarchy_retry_check.py executable [--base] --vectors` checks 4,096 cases per edition.
The original outer loop, vector construction, list writes, and retry edge bodies execute.
Region index, Cell lookup, and local reachability results are supplied.
Selected reachability callbacks change the progress cell and later cached region words.
Both original editions match every native vector and aggregate hash 082C66E3.
`test_hierarchy_retry.sh` checks the same cases and three unresolved-input cases.
An error retains completed earlier levels in its private result. It does not publish them to Simulation.
Native local reachability at 00565EB0 / 005840C0 remains open.
The regular search must also expose exact progress and retained path-buffer state before live goal binding.

## Original local reachability

`original_local_reachability` ports base 00565EB0 and expansion 005840C0.
It accepts a stable source Cell handle, hierarchy level, edition, movement zone, and initial output regions.
The original local arrays use stride eight and side lengths two, four, and eight.
The native flood preserves the LIFO stack, eight-neighbor order, visited aliases, and unique exit collection.
Each neighbor query passes its signed Cell height and original entry flags (0,1).
A nonzero entry result reads Cell +4C and the original movement compatibility table.
LandType cannot replace that category.

The final scan repeats one coordinate: source plus its masked low coordinate bits.
The two loop indexes select visited bytes. They do not change query coordinates.
Expansion instructions 00584395..0058444D prove this behavior; the base body gives the same output.
A true result means this scan found an unvisited byte with a usable, matching-region query.
It is not a general movement permission result.
A false result appends missing adjacency targets in reverse link order and preserves duplicates.
A true result preserves the existing output region list.
Only the active side-by-side corner of the visited array represents original state.
The native API zeroes its unused slots.

Callbacks must supply exact neighbors, coordinates, region lookups, signed heights, entry results, categories, and usable-area answers.
Unknown required state returns NeedsInput. Invalid levels, editions, zones, and categories return Unsupported.
The native implementation does not guess entry or map-boundary answers.

`original_local_reachability_check.py executable [--base] --vectors` checks 4,096 cases in each edition.
Compare these lines with `test_local_reachability.sh --vectors`.
They cover movement zones 0..11 and preserve aggregate hash 2F2A3C1E.
Run the original Yuri script with `--zone12 --vectors` for 128 additional cases.
Compare those lines with `test_local_reachability.sh --zone12`.
They preserve aggregate hash 67DEB132.
The original flood, compatibility table reads, vector bodies, scan, and output writes execute.
Map indexing, neighbors, actor entry, usable-area, and allocation are explicit test adapters.
`test_local_reachability.sh` checks all 4,224 vectors and four input or scan-state cases.

This resolves the local algorithm used by the outer hierarchy retry dispatcher.
Live goal binding still needs exact regular-search progress, retained path buffers, and staged-world callback inputs.

## Retained path memory and regular-search progress

`OriginalSearchMemory` exposes the original three 500-word path buffers, their counts, Search +6C, and Search +70.
The defaults follow base 00427FB0 / expansion 0042A6D0: zero words and counts, progress -1, and cell zero.
`retain_original_hierarchy_paths` applies completed hierarchy levels from two to zero.
It overwrites each successful prefix and count. It preserves every unused tail and failed lower level.
Base reset 00427E90 / expansion 0042A5B0 preserves these fields.

`find_original_ground_path` accepts an optional trailing `OriginalSearchMemory*`.
Existing callers retain their prior API behavior.
With valid resolved endpoints, the regular search sets progress zero and its cell to the source.
Each generated node compares its region with the next retained word.
A match increments progress and records that generated cell.
This happens during node generation, regardless of hierarchy filtering or the eventual result.
The progress cell cannot be replaced by the path endpoint.
The comparison sign-extends the region and zero-extends the path word.
Expansion 0042A159..0042A178 proves the comparison and writes.
Reads can cross a 500-word boundary into the next retained buffer.
The native API rejects reads beyond all three buffers instead of reading unrelated fields.
Unknown generated-cell regions return NeedsInput.
These private search-state changes do not authorize movement or publish entity changes.

`original_search_memory_check.py executable [--base] --vectors` checks 120 original vectors per edition.
The original constructor, reset, regular search, Unit entry, and path postprocessing execute.
Its four retained-word patterns include tails that remain significant beyond their logical path count.
Each vector includes progress, progress cell, all retained words and counts, output directions, and cost bits.
Compare both editions with `test_search_memory.sh base_executable expansion_executable --vectors`.
Both preserve aggregate hash B1259F48.
The native test also checks constructor values, prefix retention, unknown regions, and explicit buffer limits.

`original_hierarchy_memory_check.py executable [--base] --vectors` adds direct prefix and tail evidence.
It prefills all 1,500 words, three counts, and progress fields before original hierarchy searches.
A memory hook observes which level counts the original writes.
The checks compare every final word, count, progress field, and written-level mask.
There are 384 searches per edition, including ten failures after a completed upper level.
Both preserve aggregate hash 5D485D4E.
Compare both original outputs with `test_hierarchy_memory.sh --vectors`.
The native test checks 768 searches and twenty failed-lower-level cases across both editions.
Original builder, route, and vector bodies execute with the existing bounded storage and usable-area adapters.

`SimDecompileAt.java` accepts an optional second address for the original stack probe.
It applies Ghidra's `alloca_probe` fixup only in the transient read-only view.
Base Foot wrapper 004C3000 calls probe 00783850; expansion 004D3920 calls 007CA650.
The original instructions allocate 0x1F9C bytes and return with `ret 0xC`.
The fixup exposes the frame, but some indirect call signatures still need instruction checks.
Use `-readOnly -noanalysis`; the script must not save changes to either shared project.

## Checked single-Cell setup

`original_overlay_free_cell_setup` ports the allocated, flat, no-overlay slice of base 00472540 / expansion 0047D2B0.
Its fixed argument is -1. It updates one Cell and reads neighbors without changing them.
`CellSetupState` preserves terrain, explicit allocation, raw byte +11D, and animation bit 20000.
The existing ordered map pass shares the same terrain decision code.
It still does not claim to execute all map effect objects.

`CellSetupStatic` must describe the old TileType receiver, read before replacement.
Its bounds height comes from base 0052B690 / expansion 00547150.
The TMP header contains column count, row count, pixel width, and pixel height as its first four words.
The following pointer array selects the record with the original subtile modulo.
Bounds height starts with header height.
Record flags at +24 with mask 0x01 add record Y at +4 and subtract extra Y at +18.
The Cell stores the low byte of signed `(bounds_height - 30) / 15` at +11D.
The implementation preserves original 32-bit subtraction and signed division.

The checked branch must prove that Tube, tile animation, and shadow effects do not execute.
LandType 10 remains Unsupported.
An animation is absent when bit 20000 is known set, its index is -1, or its subtile does not match.
Shadows are absent when the flag is false or the resolved footprint is empty.
Unknown required guards return NeedsInput. Required effect construction or writes return Unsupported.
Guard receiver identity must match the old live tile index.
Replacement metadata is not read early; the old receiver supplies this call's terrain, bounds, and effect guards.

A read-only category callback receives the final terrain and supplies current original rule and occupant inputs.
The function runs `original_coarse_category`, then returns its ordered writes and both cache height values.
Only Complete supplies publishable state. Errors retain the original input state.
The caller must preserve all other Cell fields and apply the returned category and cache bytes together.
Bridge state stays unchanged, including unknown.
The function has no Unit position or RNG mutation API. It must not relocate an already placed Unit.
The general overlay, nonflat, Tube, animation, and shadow-write branches remain explicit gaps.

`original_cell_setup_check.py executable [--base] --vectors` runs 4,096 full original Cell calls per edition.
The real category producer, replacement decisions, bounds getter, and empty-shadow getter execute.
Tile headers, cell lookup, usable-area, and the constant Unit runtime kind are resolved test inputs.
The only extra service adapter suppresses C runtime exit registration.
The cases include sentinel and invalid tiles, invalid subtiles, replacement receiver retention, cliffs, and signed bounds.
Animation cases cover absent indexes, matching subtiles with bit 20000 set, and mismatched subtiles.
Shadow cases cover a false flag and a real getter that returns an empty footprint.
The check asserts unchanged placed-Unit bytes and all raw Cell flags.
All 4,096 calls include a linked Unit and the original ground occupancy value 32 at Cell +124.
The check also asserts unchanged ground and bridge heads, occupancy words, raw owner fields, Tube reference, and neighbor count.
Any original shared RNG call fails the check.
Both editions match every native vector and aggregate hash 0A4940C2.
Compare with `test_cell_setup.sh --vectors`.
The native test also checks eleven effect guards, unknown values, receiver identity, and atomic results.

## Ordinary Unit Foot path wrapper

`original_unit_basic_path` ports resolved ordinary branches of base 004C3000 / expansion 004D3920.
It accepts a resolved queue guard, actor position, goal entry, mark calls, path query, and current-cell getter.
It preserves the original 24-word queue, append offset, raw counters, and last-cell update.
The original queue guard sees the goal cell center with Z zero.
A rejected guard clears the first queue word and does not read later state.
An accepted call copies at most `24 - append_offset` words from a known nonnull result with a nonzero cost key.
A zero cost key skips the copy. Existing unused queue words remain unchanged.
Mark callback return values do not decide whether the wrapper continues.
The Unit metadata virtual is a no-op at base 0041B540 / expansion 0041C140.
The wrapper writes the frame, signed source-Y minus goal-center-Y, and zero to base +530/+534/+538.
Their expansion fields are +640/+644/+648. The second field must not be replaced by the computed distance.
The final current-cell getter supplies base +450 / expansion +558 for a nonnull path.
Entry status 6 continues without retargeting when the initial distance is no greater than resolved Rules CloseEnough.
The equality boundary follows base 004C317B..004C317F.
Entry status 7 continues when the original goal-cell Building lookup proves empty.
`FootGoalContents` preserves the lookup-enable flag and original ordered ground occupants' runtime types.
Base 004717B0 / expansion 0047C520 skips traversal when the flag is clear.
When enabled, the native code checks each known type in order and stops at the first Building type 6.
Unknown flags, links, or types remain NeedsInput. A Building reaches the bounded retarget branch below.

A known null path updates the failure timer before stopping the locomotor.
It writes the frame, retains signed source-Y minus goal-center-Y, and writes truncated `PathDelay * 900`.
Base 004C36EC / expansion 004D4016 proves those writes and the factor 900.
The rule readers identify PathDelay at base 0064C2B0 and expansion 00672AE0.
Floating arithmetic follows the checked original 53-bit, round-toward-zero behavior.
The resolved stop callback represents Foot virtual +48C/+500 and its locomotor virtual +48 call.
The native wrapper reads current actor position and bridge layer after that callback.
A nearby failure returns false when the source and goal cells differ by at most one on each axis.
The actor must be on the bridge layer, or the goal Cell bit 100 must be explicitly clear.
This branch preserves the prior last-cell field. It does not turn a failed search into movement.

Distant failure or a nearby bridge mismatch runs the original failure dispatch in order.
It calls destination virtual +410/+480 with null and true, then virtual +370/+3C8 with zero.
It then reads current owner bytes +134/+135 or expansion +1EC/+1ED.
The real owner predicate is base 004F52F0 / expansion 0050B730.
A nonzero first byte selects original mission 5.
With a zero first byte, a zero global mode also permits the second byte to select mission 5.
Otherwise the wrapper selects original mission 11, then reads the global mode again after that callback.
A nonzero mode calls the resolved House goal helper and compares the result with the explicit invalid-Cell sentinel.
A nonsentinel goal calls the destination virtual again with that Cell and true.
The callback interface preserves these raw mission numbers without assigning unproved names or behavior.
It returns the original false result and preserves the prior last-cell field.
Destination, secondary virtual, mission, and House-goal bodies remain resolved caller work.
Read-only owner and mode getters must observe private staged state after earlier callbacks.
The tested callback mutations cover actor position, owner bytes, global mode, destination, secondary slot, and mission records.
Callbacks receive no view of the kernel's private working queue, timer fields, or last-cell field.
Reading or changing those fields inside callbacks is outside the supported contract.
A real callback body with that dependency needs an explicit working-state view before live binding.
That body must remain unresolved through this API. The tests do not prove that such reads or writes are absent.

Only Complete returns publishable queue state. Error results preserve the input state.
Callbacks must use a private staged world because marking precedes later unresolved calls.
Linked-object distance, train handling, and linked-Unit propagation remain unsupported.
Unknown bridge state remains unknown. Terrain and overlay absence do not supply bridge flags.
The called guard, mark, entry, and goal-search bodies remain separate dependencies.
This module does not enable live goal commands or create missing staged-world state.

`original_foot_path_check.py executable [--base] --vectors` runs 512 full original wrapper calls per edition.
The real stack probe, coordinate getters, square root, conversion, metadata no-op, queue copy, and counters execute.
Adapters supply queue guards, goal entry, marking, cell lookup, path results, locomotor stop, and failure mutation bodies.
The original Foot stop virtual executes before the supplied locomotor stop body.
The fixture checks actor position against explicit callback effects and fails if the checked branch consumes shared RNG.
Vectors include rejected guards, every append offset, truncated paths, terminators, zero cost keys, and retained tails.
Every vector hashes queue words, raw fields, last cell, initial distance, and ordered callback arguments.
The cases include entry status 6 at the CloseEnough boundary, nearby null paths, explicit bridge layers, and seven PathDelay values.
Both editions match aggregate hash DD3D1138.
Compare both outputs with `test_foot_path.sh base_executable expansion_executable --vectors`.
The original `--ordinary` mode retains all 512 earlier nonnull vectors per edition and aggregate hash 7E173A4C.
Compare that mode with native `--ordinary-vectors`.
The original `--asymmetric` mode adds 512 nearby null paths per edition with independent X and Y offsets.
It includes zero X with nonzero Y, zero Y with nonzero X, and opposite-sign offsets.
Every accepted null path must preserve the initial Y delta at base +534 / expansion +644.
Both editions match hash 131F4268. Compare with native `--asymmetric-vectors`.
This corrects the earlier X-delta claim, which equal-axis null-path fixtures could not distinguish.
The original `--distant` mode adds 512 distant or explicit bridge-failure cases per edition, with independent X/Y offsets.
The actual owner predicate and actual Cell Building traversal execute unchanged.
The fixture includes disabled traversal with unknown links, known empty lists, and lists containing a Unit.
Destination and secondary callbacks change owner fields. Mission callbacks change the global mode before the second read.
Some stop callbacks move the actor from a distant position to a nearby position.
The initial raw Y delta must stay unchanged, while the later branch must read the new position.
Vectors compare call order, destination, mission, owner bytes, global mode, actor position, queue, and raw fields.
Both editions match hash FFDCED71. Compare with native `--distant-vectors`.
The four original native sets retain their hashes and callback-contract checks.

## Bounded Foot retarget decisions

Retargeting requires resolved UnitType MovementZone, SpeedType, and a false base +AF5 / expansion +D2C flag.
The true flag branch calls base 0047C200 / expansion 00486FF0 and remains Unsupported.
`original_retarget_movement_zone` preserves the table at base 007A1200 / expansion 007E8BF0.
The table has twelve base entries and thirteen expansion entries.
The wrapper reads the current cell twice, then queries the remapped movement component.
`FootNearestQuery` preserves the original goal, source cell, SpeedType, component, remapped zone, and actor bridge layer.
The original fixed nearest-cell arguments are 1,1,0,1,false,1,source,0.
Expansion supplies one additional final zero. The query keeps its edition explicit.

For entry status 6, the resolved invalid-Cell sentinel skips the candidate.
Otherwise the candidate's planar center distance must be strictly less than the initial actor-to-goal distance.
This comparison uses the initial distance, not Rules CloseEnough.
The goal bridge flag is read before the candidate bridge flag.
The resolved path estimate must be no greater than the maximum cell-axis gap plus six.
The estimate query preserves override -1 for the false Type flag branch.
For a Building at an entry-status-7 goal, the original skips both candidate filters.
The bounded API accepts candidates within 0..511 and rejects other nonsentinel coordinates explicitly.
It also rejects an out-of-domain status-7 candidate instead of guessing fallback Cell behavior.

An accepted candidate calls the destination virtual before marking and searching.
The later search receives that candidate as its goal.
Raw timer fields retain the original Y delta calculated before retargeting and callbacks.
Component lookup, nearest-cell search, path estimation, and destination bodies remain resolved callback dependencies.
Their original addresses are 0054F8F0/0056D230, 005502E0/0056DC20, and 0042AA30/0042D170.
The private-state and working-queue callback limits still apply. Live goal commands stay disabled.

`original_foot_retarget_check.py executable [--base] --vectors` runs 512 full original calls per edition.
It executes the actual movement-zone table, Cell Building traversal, distance comparisons, queue copy, and raw field writes.
Building runtime kind, movement component, nearest-cell result, path estimate, and mutation bodies are supplied inputs.
The cases include invalid candidates, equal-distance rejection, candidates beyond CloseEnough, estimate equality, and negative or maximal estimates.
They also combine retargeting with null-path failure and compare its private destination, mission, owner, mode, and position changes.
The base hash is D51E380B. The expansion hash is 22D0692F.
Compare both with `test_foot_path.sh base_executable expansion_executable --retarget-vectors`.
The default native test runs 5,120 vectors and sixty-eight missing-input, unsupported-branch, and private-publication checks.
