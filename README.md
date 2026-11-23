# Red Alert 2 on Linux

This project runs base Command & Conquer: Red Alert 2 1.006 as a native Linux program. It converts your copy of `game.exe` to C and uses pcrecomp for Windows calls. You need your own copy of Red Alert 2. This project is independent of Electronic Arts.

![Red Alert 2 skirmish on the native Linux build](docs/media/skirmish.gif)

[Skirmish](docs/media/skirmish.mp4) · [Campaign start and radar movie](docs/media/campaign.mp4) · [Menus](docs/media/menus.mp4)

## Status

| Area | Verification |
| --- | --- |
| Skirmish | 71 of 71 maps matched in the tests. 53 reached frame 20,000, 17 ended naturally, and one reproduced the original crash. [Report](docs/verification/skirmish.md) |
| Campaign | 26 of 26 scenarios matched with recorded orders, including two Boot Camp missions. [Report](docs/verification/campaign.md) |
| Menus, saves, and endings | Menus, skirmish cross-load in both directions, campaign save and load, and win and loss checks passed. [Report](docs/verification/ui-save.md) |
| Display | Five fixes passed checks for the map preview, slider, drop-down, score title, and control positions. [Report](docs/verification/screens.md) |
| CPU cost | 62% fewer CPU cycles per frame than the earlier native build in the tested OpenGL setup. [Report](docs/verification/performance.md) |
| Movies | All 83 archive movies finished two cycles. Two damaged clips briefly repeat the last good picture. [Report](docs/verification/movies.md) |

The [release candidate report](docs/verification/release.md) lists the final checks.

## Limits

- Online play and Yuri's Revenge are outside this release.
- Save files differ at the byte level. The tested skirmish saves load in both directions.
- The native build still uses about 2.9× the original's CPU cycles per frame under Wine in the recorded comparison.
- Campaign tests cover the recorded orders and outcomes. Five runs stop at frame 20,000.

## Credits and license

This project builds on [sp00nznet/redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp) and [sp00nznet/pcrecomp](https://github.com/sp00nznet/pcrecomp). The source snapshots are from `4ecdea4` and `91998d6`. See the [game contributors](CONTRIBUTORS.md) and the [runtime contributors](pcrecomp/CONTRIBUTORS.md) for upstream credit.

Both projects use the MIT license. See the [game license](LICENSE), [runtime license](pcrecomp/LICENSE), [game notice](NOTICE.md), and [runtime notice](pcrecomp/NOTICE).

Command & Conquer and Red Alert 2 belong to their respective rights holders. This project is unaffiliated with EA. The repository supplies source and gameplay recordings. Bring your own game executable and data.
