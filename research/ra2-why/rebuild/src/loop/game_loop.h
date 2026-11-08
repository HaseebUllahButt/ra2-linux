#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>

namespace ra2::loop {

struct State {
    std::uint32_t frame_counter = 0; // 0x00A8ED84; wraps as unsigned 32-bit state.
    bool running = true;            // 0x00A8E9A0
    bool window_active = true;      // 0x00A8ED80
    bool paused = false;            // Scenario +0x62C, local sessions only.
    bool presentation_enabled = true;
    // 0x00A83D49, 0x00A8ECD0, 0x008B41C0, 0x00A83D48, in that order.
    // Meanings/transition handlers are deliberately not guessed.
    std::array<bool, 4> pending_transitions{};
    bool transition_pending() const noexcept;
};

struct Config {
    unsigned session = 0; // 0/5: local path; 1..4: connection queue path.
    std::uint32_t local_delay_ticks = 2; // Units of timeGetTime() >> 4 (16 ms).
    std::uint32_t network_fps = 30;     // 0 selects the observed 33 ms fallback.
    bool present_during_wait = true;
};

struct PhaseTicks {
    std::uint64_t pump = 0;
    std::uint64_t service = 0;
    std::uint64_t update = 0;
    std::uint64_t dispatch = 0;
    std::uint64_t present = 0;
    std::uint64_t pace = 0;
};

class Clock {
public:
    virtual ~Clock() = default;
    virtual std::uint64_t now_ms() const = 0;
    virtual void sleep_ms(std::uint32_t milliseconds) = 0; // 0 means yield.
};

class SteadyClock final : public Clock {
public:
    std::uint64_t now_ms() const override;
    void sleep_ms(std::uint32_t milliseconds) override;
};

class SimulatedClock final : public Clock {
public:
    explicit SimulatedClock(std::uint64_t initial_ms = 0) : milliseconds_(initial_ms) {}
    std::uint64_t now_ms() const override { return milliseconds_; }
    void sleep_ms(std::uint32_t milliseconds) override;
    void advance(std::uint32_t milliseconds) { milliseconds_ += milliseconds; }
private:
    std::uint64_t milliseconds_;
};

class MessagePump {
public:
    virtual ~MessagePump() = default;
    virtual void pump(State& state) = 0;
};

enum class Message { Quit, Activate, Deactivate, Pause, Resume };

class HeadlessPump final : public MessagePump {
public:
    void post(Message message) { messages_.push_back(message); }
    void pump(State& state) override;
private:
    std::deque<Message> messages_;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void present(const State& state) = 0;
};

class HeadlessRenderer final : public Renderer {
public:
    void present(const State&) override {} // No surfaces, window, or assets.
};

struct Hooks {
    std::function<void(State&)> service;
    // Boundary for the ordered collections/vtable +0x5C calls at 0x0055AFB0.
    std::function<void(State&)> update;
    // Optional connection-queue stub, sessions 1..4 only. False ends the session.
    // No callback means an immediately successful simulated coordination.
    std::function<bool(State&)> coordinate_events;
};

enum class FrameResult { Advanced, Paused, Inactive, TransitionPending, Stopped };

class GameLoop {
public:
    using Event = std::function<void(State&)>;

    // Adapters must outlive the loop. Hooks may change state and post events;
    // calling step()/run() recursively from an adapter or hook is unsupported.
    GameLoop(MessagePump& pump, Renderer& renderer, Clock& clock,
             Config config = {}, Hooks hooks = {});
    FrameResult step();                  // 0x0055D360, modeled subset.
    std::size_t run(std::size_t budget); // 0x0048CCC0, bounded single session.
    void post_event(Event event);
    State& state() noexcept { return state_; }
    const State& state() const noexcept { return state_; }
    const PhaseTicks& ticks() const noexcept { return ticks_; }
    std::uint64_t iterations() const noexcept { return iterations_; }

private:
    bool local_session() const noexcept;
    void pump();     // 0x005D4D50
    void service();  // 0x0048D080
    void update();   // 0x0055AFB0
    void dispatch(); // 0x00647260; opaque FIFO instead of binary game events.
    void present();  // 0x004F4480 boundary; kept before update on the main path.
    void pace();     // 0x0055E160

    MessagePump& pump_;
    Renderer& renderer_;
    Clock& clock_;
    Config config_;
    Hooks hooks_;
    State state_;
    PhaseTicks ticks_;
    std::deque<Event> events_;
    std::uint64_t iterations_ = 0;
    std::uint64_t frame_start_ms_ = 0;
};

} // namespace ra2::loop
