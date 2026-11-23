# Release candidate verification

The final release candidate uses the game source at `4c94c09` and the Windows layer at `eb6bcca`. The lift produced 27,183 functions with zero errors and mapped 368 imports. This repository carries the same release fixes in its own commits.

| Area | Result |
| --- | --- |
| Skirmish | 10 original trace checks and two post-movie-fix checks pass. |
| Campaign after the movie fix | Six selected campaign checks pass. |
| Save and load | Four checks pass, including cross-load. |
| Win and loss | Both checks pass. Both score rows match. |
| Screens | All five corrected behaviors pass. |
| Movies | All 83 archive movies finish two full cycles. |
| Startup under CPU load | Both runs reach the skirmish setup screen. |

The release checks compare against recorded Wine runs. They compare scenario random state at every recorded boundary, shared census samples and the selected outcome. In the campaign checks, five missions passed before the movie fix. The sixth, Soviet mission 7, hit the old radar movie stall. All six pass with the fix.

The normal starter settings reach the main menu in 14 seconds in the documented test. With the full intro enabled, skirmish setup appears in about four minutes.

## Clean Linux installation

A fresh user profile on an x86-64 Arch Linux system tested this installer with four build jobs and privately staged build dependencies. The installer listed missing packages before setup and rejected a file with an unsupported checksum. It then converted 27,183 functions, built the native program, and installed the start command and desktop entry.

After the first launch exposed a missing Blowfish.dll, the installer gained an explicit check and copy step for that file. The revised asset step was tested with the same freshly built program. The installed launcher reached the main menu and started a skirmish on a private Xvfb display. Uninstall removed the program, start command, desktop entry and copied archives. It kept settings and the test save. The purge option removed the remaining test data.
