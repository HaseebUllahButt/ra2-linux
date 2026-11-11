# Original tank timing, 2026-11-11

These records come from the base game.exe 1.006TUC. Its SHA-256 is
`73288c03b58d370be268ca6d156b4e33bfdb2066dc980359467d8852ff3b00df`.

Each run uses two fresh stationary tanks of the same type, four cells apart.
Both facings start at zero. The target belongs to the same human house and
never returns fire. Frame zero is the stopped boundary after setting the attack
mission and target. Each following row is a complete game frame. A shot is the
first boundary after its reload timer starts. A hit is a health change.

The Grizzly first shot appears at frame 14 and the first hit at frame 24.
The Rhino first shot appears at frame 14 or 15 and the first hit at 24 or 25.
Every observed shell hits ten frames after its shot. All six runs keep the
same positions and full shooter health. Each target needs five hits.

The native tests use different houses to prevent retaliation. The same-house
shell collision path is not yet reproduced. That difference limits direct
comparison. The original sampler and native loop also label the same update
one frame apart.

The compressed JSON Lines files retain every sampled frame. The summary lists
shot and hit frames. `tank_measure.py` and `tank_validate.py` show the sampling
and checks; they use the existing `/path/to/user/Projects/ra2-oracle` helpers.

## Turning check trace

A second private match captured three fresh runs per tank and two separate
field-change tests. In all six normal runs, the tank fires during update 13.
The completed boundary is 14. The first impact is boundary 24.

Unit firing runs before turret aiming. Original addresses 006FB4DC and
006FB4E3 show that order. The firing check at 00704DBD blocks a shot when
source byte +57D is zero, saved turning byte +599 is set, and projectile ROT
is zero. The later aim update clears +599 when rotation ends. At update 11,
the turret is within the angular limit, but +599 still blocks firing.
At update 12, firing reads the old flag before the aim update clears it.
At update 13, firing passes.

Each counterfactual test cleared only +599 before update 11. Both tanks then
fired during that update. Their first impact was boundary 22. These altered
runs are separate from the six normal traces. The earlier first-shot records
show one Rhino frame of variation; the second trace did not reproduce it.

Files ending in `detail-*-trace.jsonl.gz` contain the normal call records.
Files containing `counterfactual` contain the separate field-change records.
`attack_trace.py` records them, and `validate_attack_trace.py` checks them.
These scripts use the existing original-game worker and need its setup.
