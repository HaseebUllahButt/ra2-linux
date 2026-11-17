# Campaign verification

All 26 base-game scenarios pass the recorded-order comparison. This covers 12 Allied missions, 12 Soviet missions and two Boot Camp missions.

Each case has a recorded Wine run, a fresh Wine replay and a native replay. The comparison checks scenario random state at every frame boundary, player and game orders, sampled actors and houses, and the outcome. The runs use the same seed and fix the measured frame rate at 20 for the comparison.

| Outcome | Scenarios |
| --- | ---: |
| Matched a natural defeat | 21 |
| Matched through frame 20,000 | 5 |
| Total | 26 |

The frame-limited cases are Soviet missions 1, 2 and 8, and Allied missions 7 and 11. The initial recording and Wine replay of Soviet mission 2 also reach defeat at frame 39,543. The native comparison covers their first 20,000 frames.

The final build repeats six movie-bearing missions after the [Bink fix](movies.md). All six match the recorded controls, sampled actors and outcomes. These checks cover the tested orders and outcomes. They do not certify victory routes for every mission.
