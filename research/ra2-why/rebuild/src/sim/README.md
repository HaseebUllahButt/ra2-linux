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
Production needs the complete 006F47A0 build-time formula, house multipliers, prerequisites, and placement handlers.
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
Its default is `/path/to/user/ra2-native-integration/rebuild/src`.
The shared asset reader uses its normal optimized build. SIM_TEST_FLAGS instruments the simulation and its INI/map dependencies.
It loads original C1A02MD.MAP, the original ART INI, and exact TMP metadata from original MIX archives.
Original YTNK entity 259 moves from 113,115 to 113,114 in 12 movement ticks.
Each position matches original x86 output at its real Speed=6, converted to 15.
The original IQ=3 house makes this YTNK enter Area Guard after arrival.
A second simulation replays the commands and matches each tick.
An unsupported path and an unsupported health change leave entity state intact.

## Overlay-free terrain initialization

`world.cpp` ports the raw TMP conversion and the final cliff-back branch.
Base sources are 00529300 and 00472540; expansion sources are 00544BE0 and 0047D2B0.
The raw conversion tables at base 007DE4EC and expansion 008288E4 match.
The six height tests use their exact original offsets and order.
CliffBackImpassability=2 changes eligible land types to rock when a tested neighbor is four levels higher.
Missing required neighbor heights leave the final land unresolved.
Overlay cells remain unresolved until their original overlay decisions are recovered.
Cell construction clears the bridge bit. The supported non-overlay clear/road slice keeps it clear.
LandType 10 is Tunnel. 00727FD0 creates tube links and writes Cell +116.
Bridge overlay initialization is separate and remains unrecovered.

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
