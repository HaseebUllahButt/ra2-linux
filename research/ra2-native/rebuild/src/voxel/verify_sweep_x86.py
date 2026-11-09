#!/usr/bin/env python3
"""Run base-game vehicle, shadow, occlusion, and depth comparisons."""

import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_sweep_x86.py voxel_test")
root = Path(__file__).parent
test = sys.argv[1]
failures = []


def run(script, *args):
    command = [sys.executable, str(root / script), test, *map(str, args)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        failures.append((command, result.stderr.strip()))


for name in ("htnk", "mtnk", "4tnk"):
    for facing in range(0, 256, 8):
        run("verify_vehicle_x86.py", 0, name, facing)
        run("verify_shadow_x86.py", name, facing)
        run("verify_shadow_x86.py", name, facing, "blocked")
    for direction in range(8):
        run("verify_depth_x86.py", name, direction)
    print(
        f"{name}: completed 32 vehicle and 64 shadow views; 8 depth views", flush=True
    )
for facing in range(0, 256, 8):
    run("verify_vehicle_x86.py", 16, "htnk", facing)
print("htnk: completed 32 additional vehicle views at turret offset 16", flush=True)
for command, error in failures:
    print("FAILED:", " ".join(command), error, file=sys.stderr)
if failures:
    raise SystemExit(f"{len(failures)} original x86 comparisons failed")
print("All 344 comparisons passed")
