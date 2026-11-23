# Headless game-loop model

C++17/Linux model of the control flow around `gamemd.exe`'s frame step.
It runs without game assets, Windows APIs, graphics libraries, audio, or
network access. It models phase boundaries and selected branches; it does
not reproduce the game simulation or the executable's ABI.

From `/tmp/r2` (the workspace symlink):

```sh
TMPDIR="$PWD/rebuild/src/loop" g++ -std=c++17 -O2 \
  -Wall -Wextra -Wpedantic -Werror -I rebuild/src/loop \
  rebuild/src/loop/game_loop.cpp rebuild/src/loop/test_main.cpp \
  -o rebuild/src/loop/loop_test
rebuild/src/loop/loop_test
```

No additional linker flags or dependencies are required. `test_main.cpp`
is the only `main`; `rebuild/src/loop/*.cpp` also works. Compiler temporary
files and the executable stay in this directory.

The harness checks the branches described below and runs a separate default
100-frame demonstration using `SimulatedClock`. Its output is:

```text
simulated_frames=100 iterations=100
pump=100
service=100
update=100
dispatch=100
present=100
pace=100
simulated_ms=3200
Loop checks passed.
```

## Frame behavior

The ordinary modeled path is:

```text
pump -> present (if enabled and active) -> update -> dispatch -> service
     -> test four transition flags -> increment frame_counter -> pace
```

Presentation precedes collection updates in the supplied decompilation;
the main-path service call is after event dispatch. The six phase functions
in `GameLoop` are separate so their order and boundaries remain visible.
`PhaseTicks` counts actual function entries, including calls made while
waiting. Counts can therefore differ from each other and from frame count.
Presentation is a renderer boundary, not a claim that the original function
was exclusively a screen flip.

`State::frame_counter` is unsigned 32-bit state, initialized to zero and
allowed to wrap. Updates, event callbacks, and the ordinary service see the
old frame number; pacing services see the incremented number. Each of the
four transition flags independently prevents both increment and pacing.
The flags are checked after dispatch/service so a callback can affect the
current frame. The model leaves transition resolution to its caller.

For local sessions (values 0 and 5), pause takes the shorter
`pump -> service -> present -> pace` path without updates, dispatch, or
counter advancement. An inactive local step sleeps 500 ms, pumps once,
and returns `Inactive` if still inactive. The original keeps waiting within
one frame call; returning after one wait makes each headless step usable
by an external driver. Connection sessions (values 1 through 4) sleep
10 ms and pump when inactive, then continue simulation without ordinary
presentation. The local pause flag does not freeze connection sessions.

`run(budget)` processes at most that many **iterations**, stopping when
`running` becomes false. Repeated runs continue the same session. Paused,
inactive, and transition-pending iterations consume budget without adding
a simulated frame. `FrameResult` describes the path taken; an `Advanced`
result can accompany `running == false` when dispatch ended the session
but the current frame still passed its four counter gates. This follows
the original void dispatch call rather than inventing a return-value gate.

`HeadlessPump::post` queues synthetic quit, activation, and pause messages.
The pump drains messages in order, stopping on quit. A quit seen in the
initial pump ends the step before simulation. The model deliberately polls
the pump at the start of every active iteration: the original frame routine
does not directly call the message pump unconditionally on that path.
Sessions 3/4 additionally pump inside `service`, reflecting `0x0048D1E0`.
The original can pump more often within connection-management branches.

`Hooks::update` stands in for the collection-dispatch body, including its
ordered virtual calls. `Hooks::service` supplies optional shared servicing.
Neither callback is required for the headless run. `post_event` accepts
opaque callbacks; `dispatch` executes a FIFO snapshot. Events posted by
events are deferred to the next dispatch. This snapshot rule is a model
choice, not a recovered event format or scheduling rule. In sessions 1..4,
the optional `Hooks::coordinate_events` runs first. Its absent/default case
simulates immediate success; false ends the session without executing the
batch. No sockets or real multiplayer synchronization are implemented.

## Timing

`Clock` separates time from loop behavior. `SteadyClock` uses monotonic
Linux time, `sleep_for`, and `yield`. `SimulatedClock` advances requested
sleep milliseconds immediately; a zero-duration yield advances one
simulated millisecond so waits terminate deterministically. Adapter clocks
must be monotonic and progress during waits. Timing uses 64-bit milliseconds
instead of emulating Windows' 32-bit `timeGetTime` rollover.

Local pacing compares `now_ms() >> 4` against a start tick and configurable
`local_delay_ticks`, default 2. A unit is 16 ms. It retains the decompiled
repeated `Sleep(remaining_ticks)` calls with the remaining count passed as
milliseconds, rechecking the shifted clock each time. With an aligned zero
start and no simulated work this takes 32 ms per default frame. An unaligned
start can wait less; this is a quantized clock, not an exact 60 Hz timer.

Connection pacing compares raw elapsed milliseconds against
`1000 / network_fps` (integer division), with the observed 33 ms fallback
when the rate is zero. Work already performed counts toward this duration.
While waiting it calls `service`, optionally presents when more than 10 ms
remain and the window is active, then yields. Phase counts may grow well
beyond the number of frames. Clearing `running` terminates modeled waits.
The default two shifted ticks and connection rate of 30 are explicit model
settings, not a claim that every original session runs at one fixed rate.

## Address mapping

Addresses are preferred virtual addresses, not RVAs. All implementation
functions below are in `game_loop.h/.cpp` except the application harness.
Rows labeled adapter or boundary preserve only the stated responsibility.

| Original VA | New function or state | Scope |
| --- | --- | --- |
| `0x007CD80F` | Linux C++ runtime -> `test_main.cpp::main` | CRT entry replacement; Windows CRT startup is omitted |
| `0x006BB9A0` | `test_main.cpp::run_demo` adapter construction -> `GameLoop::run` | Application-to-game handoff only; WinMain/window initialization is omitted |
| `0x0048CCC0` | `GameLoop::run` | Bounded repetition for one initialized session |
| `0x0052D9A0` | `GameLoop` construction and default `State` | Session-state initialization subset; menu/session selection is omitted |
| `0x0055D360` | `GameLoop::step` | Frame phases, inactivity, local pause, and conditional advance |
| `0x005D4D50` | `GameLoop::pump` -> `MessagePump::pump` / `HeadlessPump::pump` | Message pump boundary |
| `0x0053E770`, `0x0053E730` | `HeadlessPump::post`, `HeadlessPump::pump` | Adapter for message acquisition/removal and quit; no Win32 message structure |
| `0x0048D080` | `GameLoop::service` -> `Hooks::service` | Shared-service boundary |
| `0x0048D1E0` | `GameLoop::service` -> `pump` for sessions 3/4 | Nested pump retained; connection manager internals omitted |
| `0x0055AFB0` | `GameLoop::update` -> `Hooks::update` | Collection/vtable `+0x5C` boundary, not object AI implementations |
| `0x00647260` | `GameLoop::dispatch` | Local/connection selection and event execution boundary |
| `0x006475F0` | `GameLoop::dispatch` -> `Hooks::coordinate_events` | Connection queue boundary; successful coordination is the default stub |
| `0x0064C380` | `GameLoop::dispatch` callback batch | Event execution substitute, not binary event decoding |
| `0x004F4480` | `GameLoop::present` -> `Renderer::present` / `HeadlessRenderer::present` | Presentation boundary; internals omitted |
| `0x0055E160` | `GameLoop::pace` | Raw/shifted waits, repeated service, optional redraw, yield |
| `0x005D5890` | `Clock::now_ms` implementations | Adapter for raw millisecond time |
| `0x006C8C40` | `GameLoop::pace`: `now_ms() >> 4` | Shifted-clock timing |
| `0x007E11F0` (Sleep IAT slot) | `Clock::sleep_ms` implementations | Sleep/yield adapter, including inactive waits |
| `0x00A8ED84` | `State::frame_counter` | Conditional simulation frame counter |
| `0x00A8E9A0`, `0x00A8ED80` | `State::running`, `State::window_active` | Session and activation flags |
| `0x00A83D49`, `0x00A8ECD0`, `0x008B41C0`, `0x00A83D48` | `State::pending_transitions[0..3]` | Four distinct counter/pacing gates |
| `0x00A8B238` | `Config::session` | Session branch selection |
| `0x00A8B230` pointee `+0x62C` | `State::paused` | Local pause branch; pointer is Scenario storage per community cross-check |
| `0x00A8B558` | `Config::network_fps` | Raw pacing rate input |
| `0x00887328`, `0x00887330` | `frame_start_ms_`, `pace` raw duration | Raw start/duration subset |
| `0x00887348`, `0x00887350` | `frame_start_ms_ >> 4`, `Config::local_delay_ticks` | Shifted start/duration subset |

## Evidence and unmodeled behavior

Read the address-specific sources under `rebuild/decomp/<prefix>/`,
`analysis/engine-loop-network.md`, `analysis/community-crosscheck.md`, and
the function, edge, string, and keyword CSV indexes in
`analysis/ghidra-dump/`. The individual `FUN_006bb9a0.c` says
`decompile failed`; its available body and Main_Game handoff were read in
`analysis/ghidra-dump/landmark_decompiled.c` instead. Direct edges do not
capture the object virtual calls, so they are kept as callback boundaries.
Existing `rebuild/src/fs` interfaces/conventions informed the namespace,
standalone harness, and standard-library-only build. No network fetch was
needed; no files under `game/` were opened or executed.

Relevant callees were inspected for message acquisition/removal, service's
nested pump, the paused path, presentation/input boundaries, clocks, and
event coordination/execution. The model intentionally leaves these out:

- Rendering internals: surfaces, drawing, buffer operations, overlays,
  fades, cursor handling, and the render object's virtual updates.
- Audio, music, speech, movie playback, and periodic multimedia servicing.
- Individual simulation objects, collection identities/order, AI, scenario
  triggers, RNG state, checksum computation, and deferred deletion lists.
- Actual network transports, packet encoding, peer waits, retries, receive
  processing, frame synchronization, and per-event game semantics. The FIFO
  is unbounded and has no original ring-buffer capacity or frame timestamps.
- Replay/recording branches and their file I/O; session types 0..5 are
  accepted, but replay flags are not modeled.
- Complete pacing selection: playback shortcuts, game-speed/scenario
  overrides, connection-specific timer initialization, latency adjustments,
  the surface wait at `0x004A4830`, Windows timer-resolution calls
  `0x005D5870`/`0x005D5880`, elapsed-work accounting/statistics, and redraw
  suppression via the original modal-state globals. `presentation_enabled`
  is a synthetic ordinary/wait-path switch; pause presentation remains
  unconditional as in the inspected pause branch.
- Windows dialogs, accelerators, window procedures, COM, launcher checks,
  resources, filesystem startup, menu transitions, and session teardown.
  `0x0048C8B0` and `0x0055CFD0` were read but their modal/transition handlers
  are omitted; the four pending flags remain caller-controlled.

The strict build and harness verify 100-frame counts, observed phase order,
all four counter gates, event-triggered gates, pause/resume, inactivity,
quit, 32-bit counter wrap, callback FIFO batching, connection failure,
service/pump activity during raw waits, and frames exceeding their budget.
The same harness also passes with AddressSanitizer and UndefinedBehaviorSanitizer.
