# Stationary attack check

Build a separate test executable with `./kill-tools/build_hook.sh SOURCE_DIR`. Use an idle, disposable source directory with a completed `build-linux-game/` build and local generated C. The script restores the regular executable and sources when it exits.

Set `JOIN_KILL_OUT` to an existing empty directory. Start a private skirmish with `build-linux-game/ra2-kill --run --mute --game GAME_DIR` on Xvfb with `SDL_AUDIODRIVER=dummy`. The test sets up stationary infantry attacks and records health and reload timing.

Run `python3 kill-tools/compare.py NATIVE_RUN ORIGINAL_RUN` to check the six cases and compare timings against your reference captures. Keep capture files local.
