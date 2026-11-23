#include "game_loop.h"
#include "profile.h"
#include "trace.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace ra2::loop {

bool State::transition_pending() const noexcept {
    return std::any_of(pending_transitions.begin(), pending_transitions.end(),
                       [](bool pending) { return pending; });
}

std::uint64_t SteadyClock::now_ms() const {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void SteadyClock::sleep_ms(std::uint32_t milliseconds) {
    if (milliseconds == 0) {
        std::this_thread::yield();
    } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

void SimulatedClock::sleep_ms(std::uint32_t milliseconds) {
    // Deterministic substitute for scheduler progress after Sleep(0).
    advance(milliseconds == 0 ? 1 : milliseconds);
}

void HeadlessPump::pump(State& state) {
    while (!messages_.empty()) {
        const Message message = messages_.front();
        messages_.pop_front();
        switch (message) {
        case Message::Quit:       state.running = false; return;
        case Message::Activate:   state.window_active = true; break;
        case Message::Deactivate: state.window_active = false; break;
        case Message::Pause:      state.paused = true; break;
        case Message::Resume:     state.paused = false; break;
        }
    }
}

GameLoop::GameLoop(MessagePump& pump, Renderer& renderer, Clock& clock,
                   Config config, Hooks hooks)
    : pump_(pump), renderer_(renderer), clock_(clock), config_(config),
      hooks_(std::move(hooks)) {
    if (config_.session > 5) throw std::invalid_argument("Session must be 0..5");
}

bool GameLoop::local_session() const noexcept {
    return config_.session == 0 || config_.session == 5;
}

std::size_t GameLoop::run(std::size_t budget) {
    std::size_t steps = 0;
    while (steps < budget && state_.running) {
        step();
        ++steps;
    }
    return steps;
}

FrameResult GameLoop::step() {
    if (!state_.running) return FrameResult::Stopped;
    if(profile::enabled())profile::instance().begin_frame(state_.frame_counter);
    ++iterations_;

    if (!state_.window_active) {
        clock_.sleep_ms(local_session() ? 500 : 10);
        pump();
        if (!state_.running) return FrameResult::Stopped;
        // Original local path loops here until activated. Yield control to
        // run()/the caller after one wait so a headless step is bounded.
        if (local_session() && !state_.window_active) return FrameResult::Inactive;
    } else {
        // Portable normalization: ensure a headless quit/input source is polled
        // every iteration. The original active local path pumps in other code.
        pump();
        if (!state_.running) return FrameResult::Stopped;
        if (local_session() && !state_.window_active) return FrameResult::Inactive;
    }

    frame_start_ms_ = clock_.now_ms();
    if (local_session() && state_.paused) {
        service();
        present();
        pace();
        return FrameResult::Paused;
    }

    // The input/presentation block precedes collection updates in 0x0055D360.
    if (state_.window_active && state_.presentation_enabled) present();
    update();
    dispatch();
    service(); // 0x0055DE4A: after event dispatch, before the counter gates.

    if (state_.transition_pending()) return FrameResult::TransitionPending;
    ++state_.frame_counter; // 0x0055DE7E: not an outer-iteration counter.
    pace();
    return FrameResult::Advanced;
}

void GameLoop::pump() {
    trace::phase("pump begin",state_.frame_counter,state_.window_active,state_.paused);
    ++ticks_.pump;
    pump_.pump(state_);
    trace::phase("pump end",state_.frame_counter,state_.window_active,state_.paused);
}

void GameLoop::service() {
    trace::phase("service begin",state_.frame_counter,state_.window_active,state_.paused);
    ++ticks_.service;
    // 0x0048D1E0 pumps and services the connection manager for sessions 3/4.
    if (config_.session == 3 || config_.session == 4) pump();
    if (hooks_.service) hooks_.service(state_);
    trace::phase("service end",state_.frame_counter,state_.window_active,state_.paused);
}

void GameLoop::update() {
    trace::phase("update begin",state_.frame_counter,state_.window_active,state_.paused);
    ++ticks_.update;
    profile::Timer timer(profile::Phase::Update);
    if (hooks_.update) hooks_.update(state_);
    trace::phase("update end",state_.frame_counter,state_.window_active,state_.paused);
}

void GameLoop::post_event(Event event) {
    if (!event) throw std::invalid_argument("An event must have a callback");
    events_.push_back(std::move(event));
}

void GameLoop::dispatch() {
    trace::phase("dispatch begin",state_.frame_counter,state_.window_active,state_.paused);
    ++ticks_.dispatch;
    if (!local_session() && hooks_.coordinate_events &&
        !hooks_.coordinate_events(state_)) {
        state_.running = false;
        return;
    }
    // Snapshot the batch: events posted by an event callback run on the next
    // dispatch. These are model callbacks, not recovered NetworkEvent layouts.
    std::deque<Event> batch;
    batch.swap(events_);
    for (auto& event : batch) event(state_);
    trace::phase("dispatch end",state_.frame_counter,state_.window_active,state_.paused);
}

void GameLoop::present() {
    trace::phase("present begin",state_.frame_counter,state_.window_active,state_.paused);
    ++ticks_.present;
    {
        profile::Timer timer(profile::Phase::Present);
        renderer_.present(state_);
    }
    if (profile::enabled()) profile::instance().first_present();
    trace::phase("present end",state_.frame_counter,state_.window_active,state_.paused);
}

void GameLoop::pace() {
    ++ticks_.pace;
    // The benchmark measures work, not the frame limiter.
    if (profile::enabled()) return;
    if (local_session()) {
        const auto start_tick = frame_start_ms_ >> 4; // 0x006C8C40
        while (state_.running) {
            const auto elapsed_ticks = (clock_.now_ms() >> 4) - start_tick;
            if (elapsed_ticks >= config_.local_delay_ticks) break;
            // Preserve the decompiled Sleep(remaining shifted units) loop;
            // do not silently turn a shifted unit into a 1/60-second tick.
            clock_.sleep_ms(static_cast<std::uint32_t>(
                config_.local_delay_ticks - elapsed_ticks));
        }
        return;
    }

    const auto duration_ms = config_.network_fps == 0 ? 33 : 1000 / config_.network_fps;
    while (state_.running && clock_.now_ms() - frame_start_ms_ < duration_ms) {
        service();
        const auto elapsed_ms = clock_.now_ms() - frame_start_ms_;
        if (elapsed_ms >= duration_ms) break;
        if (config_.present_during_wait && state_.presentation_enabled &&
            state_.window_active && duration_ms - elapsed_ms > 10) {
            present();
        }
        clock_.sleep_ms(0);
    }
}

} // namespace ra2::loop
