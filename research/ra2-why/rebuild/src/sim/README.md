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
The supplied evidence does not establish base `game.exe` address or behavior parity.

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
