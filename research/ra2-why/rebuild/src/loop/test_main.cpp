#include "game_loop.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ra2::loop;

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TracePump final : MessagePump {
    explicit TracePump(std::vector<std::string>& trace) : trace(trace) {}
    void pump(State&) override { trace.emplace_back("pump"); }
    std::vector<std::string>& trace;
};

struct TraceRenderer final : Renderer {
    explicit TraceRenderer(std::vector<std::string>& trace) : trace(trace) {}
    void present(const State& state) override {
        trace.emplace_back("present:" + std::to_string(state.frame_counter));
    }
    std::vector<std::string>& trace;
};

void check_phase_order_and_gates() {
    std::vector<std::string> trace;
    TracePump pump(trace);
    TraceRenderer renderer(trace);
    SimulatedClock clock;
    Hooks hooks;
    hooks.update = [&](State& state) {
        trace.emplace_back("update:" + std::to_string(state.frame_counter));
    };
    hooks.service = [&](State& state) {
        trace.emplace_back("service:" + std::to_string(state.frame_counter));
    };
    GameLoop loop(pump, renderer, clock, {}, hooks);
    loop.post_event([&](State& state) {
        trace.emplace_back("dispatch:" + std::to_string(state.frame_counter));
    });
    require(loop.step() == FrameResult::Advanced, "Ordinary frame result");
    require(trace == std::vector<std::string>{
        "pump", "present:0", "update:0", "dispatch:0", "service:0"},
        "Presentation/update/dispatch/service order or old frame ID");
    require(loop.state().frame_counter == 1 && loop.ticks().pace == 1 && clock.now_ms() == 32,
            "Counter must advance after dispatch and before pacing completes");

    // Each original flag independently prevents BOTH counter advancement and pace.
    for (std::size_t gate = 0; gate < 4; ++gate) {
        loop.state().pending_transitions[gate] = true;
        const auto before = loop.state().frame_counter;
        const auto time_before = clock.now_ms();
        require(loop.step() == FrameResult::TransitionPending, "Transition path result");
        require(loop.state().frame_counter == before && loop.ticks().pace == 1 &&
                clock.now_ms() == time_before, "Transition gate advanced or paced");
        loop.state().pending_transitions[gate] = false;
    }
    require(loop.ticks().update == 5 && loop.ticks().dispatch == 5,
            "Transition gates must be checked after updates/events");

    loop.post_event([](State& state) { state.pending_transitions[0] = true; });
    require(loop.step() == FrameResult::TransitionPending && loop.state().frame_counter == 1,
            "Event-triggered transition must gate the same frame");
    loop.state().pending_transitions[0] = false;
    loop.state().frame_counter = std::numeric_limits<std::uint32_t>::max();
    require(loop.step() == FrameResult::Advanced && loop.state().frame_counter == 0,
            "32-bit frame counter wrap");
}

void check_pause_inactivity_and_quit() {
    HeadlessPump pump;
    HeadlessRenderer renderer;
    SimulatedClock clock;
    GameLoop loop(pump, renderer, clock);
    unsigned events = 0;
    loop.post_event([&](State&) { ++events; });
    pump.post(Message::Pause);
    require(loop.step() == FrameResult::Paused, "Pause message");
    require(loop.state().frame_counter == 0 && loop.ticks().update == 0 &&
            loop.ticks().dispatch == 0 && loop.ticks().service == 1 &&
            loop.ticks().present == 1 && loop.ticks().pace == 1 && events == 0,
            "Paused local frame must service/present/pace without simulating");
    pump.post(Message::Resume);
    require(loop.step() == FrameResult::Advanced && events == 1, "Resume drains pending events");

    loop.state().window_active = false;
    const auto before = clock.now_ms();
    require(loop.step() == FrameResult::Inactive && clock.now_ms() - before == 500 &&
            loop.state().frame_counter == 1, "Inactive local wait must not simulate");
    pump.post(Message::Activate);
    require(loop.step() == FrameResult::Advanced && loop.state().frame_counter == 2,
            "Activation after an inactive wait");
    const auto updates = loop.ticks().update;
    pump.post(Message::Quit);
    require(loop.run(100) == 1 && loop.state().frame_counter == 2 &&
            loop.ticks().update == updates, "Quit must stop before another simulation update");
    const auto iterations = loop.iterations();
    require(loop.run(100) == 0 && loop.step() == FrameResult::Stopped &&
            loop.iterations() == iterations, "Stopped loop must remain idle");
}

void check_events_and_connection_pacing() {
    HeadlessPump pump;
    HeadlessRenderer renderer;
    SimulatedClock clock;
    Config config;
    config.session = 4;
    config.network_fps = 0; // Observed fallback: 33 raw milliseconds.
    unsigned coordinated = 0;
    Hooks hooks;
    hooks.coordinate_events = [&](State& state) {
        require(state.frame_counter == coordinated, "Coordination sees the pre-increment frame");
        ++coordinated;
        return true;
    };
    // All waits must observe the new counter, not the dispatch frame.
    bool waiting = false;
    hooks.service = [&](State& state) {
        if (waiting) require(state.frame_counter == 1, "Counter increment must precede pacing service");
        waiting = true; // First service is the ordinary post-dispatch call.
    };
    GameLoop loop(pump, renderer, clock, config, hooks);
    loop.state().window_active = false;
    loop.state().paused = true; // Does not freeze a connection session.
    require(loop.step() == FrameResult::Advanced && clock.now_ms() == 43,
            "Inactive connection must wait 10 ms then simulate and pace 33 ms");
    require(coordinated == 1 && loop.ticks().service == 34 &&
            loop.ticks().pump == 35 && loop.ticks().present == 0,
            "Connection pacing must keep servicing and pumping while inactive");

    HeadlessPump local_pump;
    SimulatedClock local_clock;
    GameLoop local(local_pump, renderer, local_clock);
    std::vector<int> event_order;
    local.post_event([&](State&) {
        event_order.push_back(1);
        local.post_event([&](State&) { event_order.push_back(3); });
    });
    local.post_event([&](State&) { event_order.push_back(2); });
    local.step();
    require(event_order == std::vector<int>{1, 2}, "FIFO batch must defer events posted by events");
    local.step();
    require(event_order == std::vector<int>{1, 2, 3}, "Deferred event must execute next dispatch");

    Hooks failed_hooks;
    failed_hooks.coordinate_events = [](State&) { return false; };
    GameLoop failed(pump, renderer, clock, config, failed_hooks);
    unsigned executed = 0;
    failed.post_event([&](State&) { ++executed; });
    require(failed.run(100) == 1 && !failed.state().running && executed == 0,
            "Failed coordination must end the session without executing events");
    // The original dispatch is void: clearing running does not itself gate the
    // increment later in the current frame; only the four transition flags do.
    require(failed.state().frame_counter == 1, "Do not invent a dispatch-return counter gate");

    SimulatedClock slow_clock;
    Hooks slow_hooks;
    slow_hooks.update = [&](State&) { slow_clock.advance(100); };
    GameLoop slow(pump, renderer, slow_clock, config, slow_hooks);
    slow.step();
    require(slow_clock.now_ms() == 100 && slow.ticks().service == 1 && slow.ticks().pace == 1,
            "Over-budget frame must not wait a second full frame duration");
}

void run_demo() {
    HeadlessPump pump;
    HeadlessRenderer renderer;
    SimulatedClock clock;
    GameLoop loop(pump, renderer, clock);
    require(loop.run(0) == 0, "Zero run budget");
    require(loop.run(100) == 100 && loop.iterations() == 100 && loop.state().frame_counter == 100,
            "100-frame headless run");
    const auto& ticks = loop.ticks();
    require(ticks.pump == 100 && ticks.service == 100 && ticks.update == 100 &&
            ticks.dispatch == 100 && ticks.present == 100 && ticks.pace == 100,
            "100-frame phase tick counts");
    require(clock.now_ms() == 3200, "100 frames at the default two shifted ticks");
    std::cout << "simulated_frames=" << loop.state().frame_counter
              << " iterations=" << loop.iterations() << '\n'
              << "pump=" << ticks.pump << '\n'
              << "service=" << ticks.service << '\n'
              << "update=" << ticks.update << '\n'
              << "dispatch=" << ticks.dispatch << '\n'
              << "present=" << ticks.present << '\n'
              << "pace=" << ticks.pace << '\n'
              << "simulated_ms=" << clock.now_ms() << '\n';
}
} // namespace

int main() {
    try {
        check_phase_order_and_gates();
        check_pause_inactivity_and_quit();
        check_events_and_connection_pacing();
        run_demo();
        std::cout << "Loop checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Loop check failed: " << error.what() << '\n';
        return 1;
    }
}
