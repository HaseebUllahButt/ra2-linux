# Skirmish verification

Tests from November 2026 compare base Red Alert 2 1.006 under Wine with the native build. The breadth checks cover all 71 maps available in the standard AI battle setup.

| Test outcome | Maps |
| --- | ---: |
| Matched through frame 20,000 | 53 |
| Matched a natural ending | 17 |
| Matched an original crash | 1 |
| Total | 71 |

Dry Lake reaches the same original error at frame 17,269 after all 17,269 scenario random states match. The original repeats this result. The cause is unresolved.

A separate long-run check matches 314,694 frame boundaries and 1,046 shared unit census samples across eight games. Seven games end naturally with the same winner and end frame. Little Big Lake reaches the 75,000-frame limit in both builds.

Three additional order replays match through frame 30,000, including a nuclear launch and impact. Four save and load cases check that gameplay continues the same way in both builds. See [save and load](ui-save.md).

The comparisons cover scenario random state, sampled units and houses, and outcomes. Natural endings compare the score rows. Screen random state and frame-zero setup are outside the comparison.
