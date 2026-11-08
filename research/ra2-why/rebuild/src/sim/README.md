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
Move, Attack, Produce, Stop, and unknown commands return `Unsupported`.
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
No armor multipliers, movement rate, fire interval, or projectile motion is inferred from these inputs.
Registry indexes are not original object indexes.
Native IDs follow Structures, Units, Infantry, Aircraft order.

Original map missions, facing, infantry subcells, house ownership, and cells are retained.
Partial vehicle health uses recovered instructions.
Partial health for other categories remains unknown.
Referenced houses absent from the map receive unknown cash and a reported gap.
Local player cash stays unknown because session adjustments need more state.
Triggers, tags, teams, transports, bridges, occupancy, terrain effects, and reinforcement scripts do not execute.
The existing map reader decodes packed terrain; this core does not yet use it for movement.
Map overlays follow the existing INI reader's documented first-key policy.

## Remaining recovery

Movement needs the 004B2630 locomotor dispatch, linked Foot state, cell passability, and pathfinding handlers.
Target selection needs 006F8DF0 and its virtual eligibility/threat handlers.
004D9920 only wraps that selection. 006FCDB0 also applies transport and target eligibility rules.
Combat needs 00489180 floating-point damage operands and derived damage/destruction handlers.
The forced health primitive must not replace those handlers.
Production needs the complete 006F47A0 build-time formula, house multipliers, prerequisites, and placement handlers.
The factory primitive accepts resolved inputs to avoid inventing those numbers.
The native frame uses 64 bits and rejects overflow; the original frame counter wraps at 32 bits.
The core does not claim replay parity with original network events or original autonomous object updates.

## Recovered ground movement state

`ground.h` exposes `GroundMotion`, `GroundGuards`, and world coordinates.
These are verified movement primitives. They do not execute map movement ticks.
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
Interpolation currently supports exact binary radii within a documented numeric domain.
Other radii and uncertain x87 integer boundaries throw errors.
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
