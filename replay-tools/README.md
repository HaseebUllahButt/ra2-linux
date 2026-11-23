# Skirmish census

Collect counts and unit types from Red Alert 2 1.006, then compare samples at the same game frame.

- Run `python3 replay-tools/census_x.py --help` for capture options. For a native capture, pass `--native-cmd` with a command that starts your build with `--mute` and `--game` set to a private game directory. Use a private Xvfb display.
- For a Wine capture, supply a private `--oracle` directory with `game/`, `prefix/`, and your own `recomp-trace/win/rclaunch.exe` and `rec.dll` capture tools.
- Run `python3 replay-tools/verify-replay.py ORIGINAL_CENSUS NATIVE_CENSUS` to compare country, cash, unit counts and types at shared frames. Use `cmpcensus.py` for a concise list of differences.
- Run `python3 replay-tools/cmphash.py ORIGINAL_HASH NATIVE_HASH` to compare reference recorder hashes.

These tools expect the same map, seed and initial settings. Keep captures local. A census result covers the fields and frames in the comparison.
