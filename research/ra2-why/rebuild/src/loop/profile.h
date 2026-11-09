#pragma once
// Native benchmark probe. Off unless the RA2_BENCH environment variable is set.
// Each Scope adds its wall time to one phase. Nested Scopes inside a timed
// game-update phase count toward the outer phase only. The report prints at
// process exit. No game state is read or changed.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace ra2::loop::profile {

enum class Phase : int {
    Update, Movement, Combat, Economy, AI, Production, Tech,
    Present, Scene, Radar, Count
};

using Clock = std::chrono::steady_clock;

struct Stat {
    double total_ms = 0, worst_ms = 0;
    std::uint64_t calls = 0;
};

class Profile {
public:
    Profile() : start_(Clock::now()) {
        const char* value = std::getenv("RA2_BENCH");
        enabled_ = value && *value && *value != '0';
    }
    ~Profile() { if (enabled_) report(); }
    bool enabled() const { return enabled_; }
    void add(Phase phase, double ms) {
        auto& stat = stats_[std::size_t(phase)];
        // Name each slow update or present with its call number.
        if ((phase == Phase::Update || phase == Phase::Present) && ms >= 50)
            std::fprintf(stderr, "RA2_BENCH slow %s call=%llu ms=%.1f\n", phase == Phase::Update ? "update" : "present",
                static_cast<unsigned long long>(stat.calls + 1), ms);
        stat.total_ms += ms; stat.worst_ms = std::max(stat.worst_ms, ms); ++stat.calls;
    }
    void first_present() {
        if (startup_ms_ < 0)
            startup_ms_ = std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
    int depth = 0;
    double excluded_ms = 0; // Probe work, such as world hashes, not game work.
private:
    void report() const {
        static const char* names[] = {"update (all)", "  movement", "  combat", "  economy", "  ai",
            "  production", "  tech", "present (all)", "  scene", "  radar"};
        std::fprintf(stderr, "RA2_BENCH startup_to_first_present_ms=%.1f\n", startup_ms_);
        std::fprintf(stderr, "RA2_BENCH %-16s %8s %10s %10s\n", "phase", "calls", "avg_ms", "worst_ms");
        for (std::size_t i = 0; i < stats_.size(); ++i) {
            const auto& s = stats_[i];
            std::fprintf(stderr, "RA2_BENCH %-16s %8llu %10.3f %10.3f\n", names[i],
                static_cast<unsigned long long>(s.calls), s.calls ? s.total_ms / double(s.calls) : 0.0, s.worst_ms);
        }
    }
    Clock::time_point start_;
    bool enabled_ = false;
    double startup_ms_ = -1;
    std::array<Stat, std::size_t(Phase::Count)> stats_{};
};

inline Profile& instance() { static Profile profile; return profile; }
// Construct the probe during static initialization, close to process start.
inline const bool started = (instance(), true);
inline bool enabled() { return instance().enabled(); }

// A game-update phase. Only the outermost phase records time.
class Scope {
public:
    explicit Scope(Phase phase) : phase_(phase), on_(enabled()) {
        if (!on_) return;
        outer_ = instance().depth++ == 0;
        if (outer_) {start_ = Clock::now(); excluded_ = instance().excluded_ms;}
    }
    ~Scope() {
        if (!on_) return;
        --instance().depth;
        if (outer_) instance().add(phase_, std::chrono::duration<double, std::milli>(Clock::now() - start_).count() -
            (instance().excluded_ms - excluded_));
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Phase phase_;
    bool on_, outer_ = false;
    Clock::time_point start_;
    double excluded_ = 0;
};

// A top-level or drawing phase. Always records, independent of nesting.
class Timer {
public:
    explicit Timer(Phase phase) : phase_(phase), on_(enabled()) {
        if (on_) {start_ = Clock::now(); excluded_ = instance().excluded_ms;}
    }
    ~Timer() {
        if (on_) instance().add(phase_, std::chrono::duration<double, std::milli>(Clock::now() - start_).count() -
            (instance().excluded_ms - excluded_));
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
private:
    Phase phase_;
    bool on_;
    Clock::time_point start_;
    double excluded_ = 0;
};

// Probe-only work inside a timed phase. Its time is removed from every
// enclosing Timer and Scope.
class Exclude {
public:
    Exclude() : on_(enabled()) { if (on_) start_ = Clock::now(); }
    ~Exclude() {
        if (on_) instance().excluded_ms += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
    Exclude(const Exclude&) = delete;
    Exclude& operator=(const Exclude&) = delete;
private:
    bool on_;
    Clock::time_point start_;
};

} // namespace ra2::loop::profile
