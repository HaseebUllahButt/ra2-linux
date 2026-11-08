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

The asset test reads base `rules.ini`: HTNK uses 120mm; its damage is 90,
ROF 65, projectile Cannon, speed 40 and warhead AP. Cannon is arcing and
checks cliffs, elevation and walls. Gravity is 6 in `[AudioVisual]`. The x86
test compares 40 launch speeds, 235 launch angles, 517 headings, 514 horizontal
vectors, 512 speed limits, 512 pitch vectors, 515 Lobber branches, 256 connected
launches, 1,024 arc proposals, 256 fire events, 96 impact checks, 1,024 damage recipients, 12 impact gates and 2,048 first terrain candidates. The
connected launch uses a resolved branch stub. Terrain trace and impact height
are resolved stubs.
