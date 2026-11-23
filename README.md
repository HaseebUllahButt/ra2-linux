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

## Requirements

- An x86 Linux system that runs 32-bit programs.
- Your installed base Red Alert 2 files, including `Blowfish.dll`. The installer accepts the tested 1.006 executable with SHA-256 `73288c03b58d370be268ca6d156b4e33bfdb2066dc980359467d8852ff3b00df`.
- GCC with 32-bit support, CMake, Ninja, pkg-config, Python 3, and Liberation Sans fonts.
- 32-bit SDL2, SDL2_ttf, and FFmpeg development libraries. The installer names the missing tools and packages for Arch, Debian, Ubuntu, and Fedora.
- Space for a local copy of the game files and a temporary build. The first build takes tens of minutes. Python can use installed Capstone and pefile modules or download them into a temporary environment.

## Install

```sh
git clone https://github.com/HaseebUllahButt/ra2-linux.git
cd ra2-linux
./install.sh
```

Use your own copy of base Red Alert 2. The installer reads local game files and does not download them.

- **CD or your own ISO.** Mount your disc or image. If it contains setup files, install the game from the media first. Run `./install.sh --game-dir "/path/to/Red Alert 2"` with the folder that contains `game.exe`, `Blowfish.dll`, and the game archives. You can also give a mounted folder that contains those files.
- **Steam.** Install Red Alert 2 from *Command & Conquer: The Ultimate Collection*. Run `./install.sh` to look in Steam libraries and Proton prefixes. To select the folder yourself, use **Manage → Browse local files** in Steam and pass that path with `--game-dir`. [Official Steam store](https://store.steampowered.com/bundle/39394/Command__Conquer_The_Ultimate_Collection/)
- **EA app.** Install Red Alert 2 from *Command & Conquer: The Ultimate Collection* in a Wine or Lutris prefix. Run `./install.sh` to look in those prefixes. For a custom location, pass the installed game folder with `--game-dir`. [Official EA store](https://www.ea.com/games/command-and-conquer/command-and-conquer-the-ultimate-collection)

The build and gameplay checks used a **CD-based 1.006 copy**. Steam and EA app copies are **untested**. The installer checks the selected `game.exe` against the tested SHA-256 and stops if it differs.

The installer asks for a path if it finds several copies or no copy. It copies the required files, converts the code, and builds the Linux program. It installs under `~/.local/share/ra2-linux` and adds a desktop entry. Use `--jobs 4` to limit the number of build jobs.

Run `./install.sh --uninstall` to remove the game program, copied files, and launchers. Add `--purge` to also remove local saves and settings.

## Play

Open **Red Alert 2 (Native Linux)** from your desktop menu or run either command.

```sh
~/.local/bin/ra2-linux
./play.sh --scale integer
```

Integer scale shows a centered 800×600 picture in a 1024×768 window and uses whole-number multiples in larger windows. The installed game keeps settings and saves in `~/.local/share/ra2-linux/game/`. The first launch skips the long intro. Set `Play=yes` under `[Intro]` in your installed `RA2.INI` to watch it.

## How it works

The static recompiler finds x86 functions in `game.exe` and converts them to C before compilation. The release candidate converted 27,183 functions. The Linux executable loads the original game data at run time. [pcrecomp](pcrecomp/) supplies the Windows layer and uses SDL2 for the display and input, and FFmpeg for Bink movies.

## How we checked gameplay

We ran the original 1.006 game under Wine with the same setup and seeded random state. We compared the scenario random number generator at each recorded game frame. We also compared a census of houses and units, including cash, object types and health where the capture recorded them. Recorded orders drove the campaign checks. We compared matching end frames and score rows when a game ended naturally.

See the [order replay tools](tools/order-replay/), [census tools](replay-tools/), and [frame sampler](sampler-tools/) for capture and comparison code.

## Project history

- [First static recompiler](research/ra2-recomp/). This attempt lifted x86 functions and tested the code that connected them to the operating system.
- [Engine rebuild](research/ra2-why/). This route built a separate Linux engine for game rules, rendering, and skirmish play.
- [Native production prototype](research/ra2-native/). This work connected deployment, building placement, and unit production to the shared game world.

The release uses static recompilation on the community base. It carries over the original game logic and uses pcrecomp for Windows calls. We checked the result against the original in skirmish and campaign tests. Use the root installer for the playable release.

## Credits and license

This project builds on [sp00nznet/redalert2-recomp](https://github.com/sp00nznet/redalert2-recomp) and [sp00nznet/pcrecomp](https://github.com/sp00nznet/pcrecomp). The source snapshots are from `4ecdea4` and `91998d6`. See the [game contributors](CONTRIBUTORS.md) and the [runtime contributors](pcrecomp/CONTRIBUTORS.md) for upstream credit.

Both projects use the MIT license. See the [game license](LICENSE), [runtime license](pcrecomp/LICENSE), [game notice](NOTICE.md), and [runtime notice](pcrecomp/NOTICE).

Command & Conquer and Red Alert 2 belong to their respective rights holders. This project is unaffiliated with EA. The repository supplies source and gameplay recordings. Bring your own game executable and data.
