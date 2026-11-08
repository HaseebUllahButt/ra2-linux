# Base tank weapon primitives

The API is in `weapons.h`. Build from the parent checkout root after its `sim/`
files are present:

```
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/{weapons,test_main}.cpp \
  rebuild/src/sim/{combat,target,distance}.cpp -o /tmp/ra2_weapons_test
/tmp/ra2_weapons_test
python3 rebuild/src/weapons/verify_base_x86.py /tmp/ra2_weapons_test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -I rebuild/src \
  rebuild/src/weapons/asset_test.cpp \
  rebuild/src/fs/{mix_bootstrap,mix_archive,filename_hash}.cpp \
  rebuild/src/rules/ini_file.cpp -o /tmp/ra2_weapons_assets
/tmp/ra2_weapons_assets
```

The x86 test needs Unicorn and `/path/to/user/ra2/game/game.exe`. Asset tests use
`/path/to/user/ra2/game`. No image or screenshot is produced.

`assess_target_for_scan()` calls the simulation's original target-scan prefix.
A continuing prefix does not approve firing. `base_resolved_tank_fire()` emits
a projectile spawn event only for a caller that has already resolved firing
eligibility, target, muzzle, weapon, damage and IDs. It matches the base
`0x6CAC65` fire fragment for ROT=0. It uses `0x733F70` and `0x47FB70` to
calculate launch speed from horizontal distance and gravity. The table input
must be read from base `game.exe`. The event retains muzzle and target
coordinates. It does not calculate the launch angle or initial velocity.

`base_arc_proposal()` matches `0x460397..0x460435`. It subtracts gravity from
vertical velocity, then applies velocity to the integer position. It returns a
proposal before the original terrain and collision logic. It is not a complete
projectile tick. `base_cannon_impact()` matches the Cannon branch of
`0x461C10` after its terrain trace and height have been resolved. An unknown
trace returns `NeedsTrace`. Cannon has `Level`, `FlakScatter` and `AA` off.
It has cliff and wall checks on. These flags and its arcing setting come from
the shipped rules, not this API's defaults.

`resolved_damage_event()` calls `sim::total_damage()` with caller-resolved
warhead, armor and distance inputs. Simulation owns health and event
application. Area target collection at `0x47E2C0`, derived health handlers,
death effects and impact ordering remain open. Do not use the event as proof
that the original damage dispatch completed.

The asset test reads base `rules.ini`: HTNK uses 120mm; its damage is 90,
ROF 65, projectile Cannon, speed 40 and warhead AP. Cannon is arcing and
checks cliffs, elevation and walls. Gravity is 6 in `[AudioVisual]`. The x86
test compares 40 launch speeds, 1,024 arc proposals, 256 fire events and 96
impact checks. Its terrain trace and impact height are resolved stubs.
