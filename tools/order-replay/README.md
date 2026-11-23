# Order replay

Capture player orders, scenario random state, and a census of houses and units. Compare recordings from the 1.006 game in Wine and from the native build.

- Build the native observer with `python3 tools/order-replay/build_join.py SOURCE_DIR`. Use a source directory with locally generated C and a completed Ninja build in `build-linux-game/`. This leaves the regular executable intact.
- Build the reference DLL with `tools/order-replay/build_dll.sh`, clang, lld-link and Wine's i386 import libraries. Load it into your own matching game executable under Wine with a local test launcher.
- Set `ORDERS_OUT` to an existing empty output directory. See `src/orders_core.h` for seed, speed, limits and replay input settings. Run on a private display with dummy audio.
- Run `python3 tools/order-replay/orrun.py compare ORIGINAL_RUN NATIVE_RUN` for a comparison. Use `decode` or `inputs` to inspect or extract captured orders.

The observer forces a fixed measured frame rate for exact simulation comparisons. It writes addresses and raw game state in its output. Keep the output local.
