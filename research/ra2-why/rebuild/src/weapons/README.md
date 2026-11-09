# Base tank weapon primitives

The API is in `weapons.h`. Run the native checks from the project root:

```sh
make -C rebuild weapons
python3 rebuild/src/weapons/verify_base_x86.py rebuild/build/weapons_test
python3 rebuild/src/weapons/verify_trace_x86.py rebuild/build/weapons_test
python3 rebuild/src/weapons/verify_object_x86.py rebuild/build/weapons_test
python3 rebuild/src/weapons/verify_tick_x86.py rebuild/build/weapons_test
make -C rebuild build/weapons_fractional_tick_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_fractional_x86.py rebuild/build/weapons_fractional_tick_test
make -C rebuild build/weapons_near_ground_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_near_ground_x86.py rebuild/build/weapons_near_ground_test
make -C rebuild build/weapons_area_impact_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_area_impact_x86.py rebuild/build/weapons_area_impact_test
make -C rebuild build/weapons_occupant_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_occupant_x86.py rebuild/build/weapons_occupant_test
make -C rebuild build/weapons_object_damage_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_object_damage_x86.py rebuild/build/weapons_object_damage_test
make -C rebuild build/weapons_object_damage_path_test
RA2_GAME_DIR="$PWD/game" python3 rebuild/src/weapons/verify_object_damage_path_x86.py rebuild/build/weapons_object_damage_path_test

```


Set `RA2_GAME_DIR` when the owned assets are outside the project game folder.
The optional x86 check needs Python Unicorn and the owned base executable at
`game/game.exe`. The asset check reads the base archives. No image is produced.

`assess_target_for_scan()` calls the simulation's original target-scan prefix.
A continuing prefix does not approve firing. `base_resolved_tank_fire()` emits
a projectile spawn event only for a caller that has already resolved firing
eligibility, target, muzzle, weapon, damage and IDs. It matches the base
`0x6CAC65` fire fragment for ROT=0. It uses `0x733F70` and `0x47FB70` to
calculate launch speed from horizontal distance and gravity. The table input
must be read from base `game.exe`. The event retains muzzle and target
coordinates. It does not calculate the initial velocity.

`BaseBallisticAngles::angle()` implements `0x47F8B0` and `0x47F9B0`. It loads
the base executable's angle table. A missing result indicates no usable
solution or an unresolved second-solution value. Inputs outside the shared
square-root helper's verified domain throw. The caller must supply the
original branch selection. `tank_launch()` combines the original heading,
speed limit, horizontal vector and pitch. It covers normal non-burst Cannon
launches with caller-resolved displacement and branch selection. It returns
the initial velocity without changing simulation state.
`base_lobber_branch()` resolves the original `0x6D7D20` branch. It checks
the weapon's `Lobber` rule and the actor's current target position. It does
not decide firing eligibility. The base HTNK 120mm weapon has `Lobber=false`.

`base_arc_proposal()` matches `0x460397..0x460435`. It subtracts gravity from
vertical velocity, then applies velocity to the integer position. It returns a
proposal before the original terrain and collision logic. It is not a complete
projectile tick. `base_cannon_impact()` matches the Cannon branch of
`0x461C10` after its terrain trace and height have been resolved. An unknown
trace returns `NeedsTrace`. Cannon has `Level`, `FlakScatter` and `AA` off.
It has cliff and wall checks on. These flags and its arcing setting come from
the shipped rules, not this API's defaults.

`base_trace_terrain()` implements the base `0x4BBAF0` terrain trace for a
stable map. It converts the position to original cell coordinates and asks the
caller for the resolved cell. The original later path looks up the same cell
and repeats the same checks. Unknown map cells return `NeedsCell`. The caller
must supply cell identity, the original floor result, wall type, owner and alliance.
That floor result uses Cell +0x140 bit 0x80. The simulation's separate
`TerrainCell.bridge` value describes bit 0x100 and cannot supply this bit.
The original out-of-map fallback cell also needs a resolved lookup. Projectile
object collision and the complete projectile tick remain outside this API.
`verify_trace_x86.py` checks the full original function with resolved map and
object stubs. It covers 4,117 cases, including the distance boundary and cell
coordinate wrap.

`base_projectile_object_collision()` selects from the ground cell's original
object list order. The cell list, object positions, eligibility flags and
alliances must be resolved by the caller. A source or allied object can mask
another object because selection occurs before those checks.

`ContentLinks` models original cell heads and shared object links separately
from the simulation. One object link can affect several footprint cells and
both layers. It preserves earlier writes when a later traversal finds a cycle.
The original comparison covers 2,298 footprint operations and the map loader
order. Footprints and placement callbacks remain supplied test inputs. This
model does not establish live cell occupancy or enable combat.

`base_flat_cannon_tick()` connects movement, target proximity, object
collision, map bounds and the terrain impact decision for a guarded slice. It
returns typed decisions and does not change simulation state. It needs the
original cell snapshots, building-interception result and terrain lookup.
Near-ground motion without a source, bridge crossings, building interception,
and slow starting velocity return `NeedsInput`. Other projectile types, visual effects, damage
dispatch and the full lifecycle are outside this slice. The test compares
1,536 connected base x86 cases. The original floor, map, building, alliance,
height and terrain trace results use resolved stubs. A separate test compares
2,048 object selection and collision cases against the original functions.
`verify_fractional_x86.py` compares 32 original fractional launch vectors and
eight consecutive updates per vector. All 256 snapshots match the original
position and velocity. The world, height, and terrain trace remain supplied
test inputs. This check does not prove a full lifecycle.

`bind_flat_tick_cells()` reads floor and bridge state from initialized, flat
simulation cells. Unknown metadata, overlays, ramps, and bridges block binding.
The near-ground check uses 32 initialized ALL02S.MAP cells and compares 288 ticks
with the original executable. The floor comes from the real map. Height,
object, building, and terrain trace results remain supplied test inputs.

`base_area_impact_enabled()` matches the original area-damage entry gate.
`base_standard_area_impact()` builds the standard detonation request with its
original wrapping damage multiply and final area flag. Special warheads and
earlier effects must be resolved and absent. The original comparison checks
1,073 complete area calls, including negative damage and integer overflow.
Target adjustment, recipient collection, and projectile removal remain open.
`base_resolved_impact_recipient()` matches the final recipient checks and returns
a damage request. Resolve recipient state again after each request. The caller
must collect candidates in original order.

`resolved_damage_event()` calls `sim::total_damage()` with caller-resolved
warhead, armor and distance inputs. Simulation owns health and event
application. Area target collection at `0x47E2C0`, derived health handlers,
death effects and impact ordering remain open. Do not use the event as proof
that the original damage dispatch completed.

`object_damage.h` contains isolated base damage and Unit death plans. They
stop before callbacks and do not change Simulation health. Original checks
cover 4,096 shared prefixes, 2,059 Unit entry chains, and 120 death branches.
The armor result and virtual checks use supplied test values. Derived damage,
triggers, death effects, and removal remain required before live integration.
`base_object_damage_path()` also models the shared handler's callback order.
Its callback must run the real trigger or virtual handler and update the
snapshot before processing continues. Lethal infantry damage without bypass
remains unsupported. An unknown required object kind also blocks that path.
The original comparison covers 4,096 paths with supplied callback changes.
Unit and Techno derived handlers and real trigger execution remain separate.

The asset test reads base `rules.ini`: HTNK uses 120mm; its damage is 90,
ROF 65, projectile Cannon, speed 40 and warhead AP. Cannon is arcing and
checks cliffs, elevation and walls. Gravity is 6 in `[AudioVisual]`. The x86
test compares 40 launch speeds, 235 launch angles, 517 headings, 514 horizontal
vectors, 512 speed limits, 512 pitch vectors, 515 Lobber branches, 256 connected
launches, 1,024 arc proposals, 256 fire events, 96 impact checks, 1,024 damage recipients, 12 impact gates and 2,048 first terrain candidates. The
connected launch uses a resolved branch stub. Terrain trace and impact height
are resolved stubs.

The live-world adapters are in `sim/damage_runtime.h`. The ground collision
adapter follows the session snapshot's Cell +E4 head and shared Object +30
links. Its resolver supplies original collision flags, virtual positions and
alliance. Missing links, cells, or selected-object state block the result. The
original comparison covers 2,048 object selections and collisions. It does not
claim a complete projectile update.

`base_small_ground_area_candidates()` collects Unit recipients in the original
cell and link order for CellSpread up to 0.5. It preserves repeated IDs and
excludes the source. It stops on buildings, other object kinds, overlays,
bridges, airborne collection, special groups and unknown state. The original
comparison covers 600 area collections. It does not run damage callbacks or
other detonation effects. `base_advance_flat_projectile()` retains projectile
state across the existing guarded Cannon ticks. It stops at impact until the
original impact and removal callbacks are recovered. It preserves state on an
unknown tick. A replay check covers 32 launches and 256 original tick snapshots.

`base_fire_eligibility_prefix()` follows the first 006C9260 checks and returns
original rejection codes. Continue does not permit firing: later eligibility,
range, facing and callback paths still need the original checks. Its comparison
covers 5,000 original paths. `base_fire_timer_ready()` is the separate original
Techno +244/+24C timer check, with 4,096 comparisons. The one-shot Unit reload
calculation uses weapon ROF, the House multiplier, veteran state and rules
multiplier. It advances the shared RNG with the original inclusive 0..2 draw.
The comparison covers 2,000 delays and the complete RNG table and indexes.
Call it only after the original shot and burst decisions succeed.

`base_techno_damage_prefix()` implements the original Techno damage multiplier,
minimum damage, same-type immunity, invulnerability, virtual immunity and
warhead immunity before Object damage. Type +5D0 side effects still block that
path. The original comparison covers 5,000 prefix cases. Foot and Unit derived
effects, Object callbacks, trigger execution and death/removal are not complete.
These APIs do not enable live Attack or replace the derived damage handlers.

`base_ordinary_unit_fire_check()` continues that prefix through the original
Unit +364 check. It supports a primary weapon against a Unit on ordinary land,
with matching bridge flags, standard timer, and the listed special weapon and
warhead flags clear. It also requires the separately selected secondary slot's
live effect checks to be clear. A zero result passes this one original virtual
check; range, turret facing, target assignment and projectile creation remain
separate. The original comparison covers 3,000 complete checks and all seven
results in this slice. The shipped 120mm uses the constructor default
DecloakToFire=true, so the live shooter cloak state must be resolved. Unknown inputs block the decision.

`combat_runtime_regression.cpp` is the fixed native regression entry point. Pass
it the owned base `game.exe` path. It checks firing, reload RNG replay, Techno
damage guards, shared cell order, allied masking, projectile replay and unknown
state rejection. The expected replay values were checked against game.exe.

`base_cannon_range()` implements the normal ground Cannon branch of the original
006C4BB0 range check. It checks minimum distance, horizontal range, the
ballistic height solution and a caller-resolved 004BBAA0 line trace. It rejects
unknown flags, gravity, bridge state or trace. Its comparison covers 5,033
original cases. Target and muzzle position selection, the real line trace and
turret facing remain separate.

`base_cannon_line_trace()` runs the original 004BBAA0 firing-line traversal
using resolved Cell identities, floors, wall state, overlay indices and
alliances. It follows the original integer sampling order and returns unknown
for missing cells. It includes the original first-hit wall exception. The
comparison covers 2,400 complete game.exe traces. The loaded world must still
supply Cell +140 bit 80, wall ownership and alliance state.

`base_weapon_range_from_cells()` converts a resolved original INI double with
00469AE0 semantics. A value of -1 retains the supplied prior value. The base
120mm Range of 5.75 resolves to 1472 world units. The owned asset check also
checks AP spread, damage falloff and the absent Burst and DecloakToFire keys.
The original WeaponType constructor sets those two absent fields to one. The
original comparison checks those defaults and 3,003 range conversions.

`base_projectile_damage()` recovers the 006CAB7C projectile damage calculation.
It applies House and object firepower, truncates, then applies the relevant
veteran firepower multiplier. It includes the original sonic and fire-particle
zero-damage branches. The comparison covers 5,000 original cases. Target
armor and derived target damage remain separate.

`base_stationary_unit_aim()` binds the original 006D65B0 stationary Unit aim
point to a placed session entity's stored position. It requires the original
locomotor movement result and rejects unknown or moving targets. The Unit
vtable's +58 delegates to +48, which reads this position. The original check
covers 2,000 aim points. Moving-target lead remains unresolved.

`base_flat_unit_muzzle()` implements the original 006C1830 muzzle position for
flat drive Units. It uses independent original hull and turret directions,
weapon-slot FLH, UnitType turret offset and the live burst index. It checks the
flat locomotor and arithmetic bounds. The comparison covers 3,000 varied cases
and all 1,024 quantized hull/turret direction pairs. Base art.ini supplies
HTNK PrimaryFireFLH=150,0,100. Slope and other locomotor transforms remain
unknown and are rejected.

`base_next_area_damage()` walks collected recipients in order. It refreshes each
recipient immediately before the damage request, and waits for the caller to
finish the real derived damage callback before advancing. Repeated IDs remain
repeated. Missing recipient state blocks the cursor. This is dispatch support;
it does not execute triggers, damage, death or projectile removal.

`base_unit_cannon_impact_position()` recovers the ordinary ground Unit impact
position adjustment in 00461DE0. It checks original distance and rejects unknown
projectile, target or warhead state. The original comparison covers 3,000
positions. `base_complete_single_cluster_scatter()` preserves the original
post-impact RNG operations after a completed area callback. For an active
projectile, the final cluster still draws 256..512 and one direction word.
Inactive projectiles draw neither. All 250 words and both indexes match the
original in 1,200 checks. The caller must prove the single-cluster, non-airburst
path and complete every original impact callback before this step.
