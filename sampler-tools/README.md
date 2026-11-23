# Frame and damage sampler

Build a separate native test executable with `./sampler-tools/build_sampler.sh SOURCE_DIR`. Use an idle, disposable source directory with a completed `build-linux-game/` build and local generated C. The script adds the observer to that directory for one build and then restores the regular executable and sources. It verifies the regular executable's SHA-256.

Set `JOIN_SAMPLER_OUT` to an existing empty output directory. Run `build-linux-game/ra2-scenario-sampler --run --mute --game GAME_DIR` on a private Xvfb display with `SDL_AUDIODRIVER=dummy`. The sampler records frame boundaries, scenario state and damage events. Set `JOIN_SAMPLER_CENSUS=1` for object samples.

- Check the samples with `python3 sampler-tools/verify_sampler_columns.py RUN [REFERENCE_DIR]`.
- Compare them with `python3 sampler-tools/compare_sampler.py RUN_A RUN_B [FIRST_FRAME LAST_FRAME [REFERENCE_DIR]]`.

The optional reference directory contains local original captures for the same seed and map. Keep raw state and capture files local.
