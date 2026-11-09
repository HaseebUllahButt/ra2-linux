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
#include <ctime>
#include <sched.h>

namespace ra2::loop::profile {

enum class Phase : int {
    Update, Movement, Combat, Economy, AI, Production, Tech,
    Present, Scene, Radar, HudRefresh, HudDraw, Upload, SceneState, Validation, Orders, AutoPlace, Autoplay, Count
};

using Clock = std::chrono::steady_clock;

struct Stat {
    double total_ms = 0, worst_ms = 0;
    double total_cpu_ms = 0, worst_cpu_ms = 0;
    std::uint64_t calls = 0;
};

inline double cpu_ms() {
    // Sum CPU work across this process's threads. Scheduling waits add only
    // wall time; parallel work can use more CPU time than wall time.
    timespec value{};
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &value);
    return double(value.tv_sec)*1000.0 + double(value.tv_nsec)/1000000.0;
}

class Profile {
public:
    Profile() : start_(Clock::now()) {
        const char* value = std::getenv("RA2_BENCH");
        enabled_ = value && *value && *value != '0';
    }
    ~Profile() { if (enabled_) report(); }
    bool enabled() const { return enabled_; }
    std::uint64_t frame() const { return frame_; }
    bool in_frame() const { return in_frame_; }
    void begin_frame(std::uint64_t frame) { in_frame_=true;frame_=frame; frame_ms_.fill(0); }
    void add(Phase phase, double ms, double cpu) {
        auto& stat = stats_[std::size_t(phase)];
        static const char* names[]={"update","movement","combat","economy","ai","production","tech",
            "present","scene","radar","hud_refresh","hud_draw","upload","scene_state","validation","orders","auto_place","autoplay"};
        frame_ms_[std::size_t(phase)]+=ms;
        const double limit=phase==Phase::Present?8.0:4.0;
        if(ms>=limit) {
            const auto log_start=Clock::now();
            const auto log_cpu=cpu_ms();
            std::fprintf(stderr,"RA2_BENCH slow %s frame=%llu call=%llu wall_ms=%.3f cpu_ms=%.3f",
                names[std::size_t(phase)],static_cast<unsigned long long>(frame_),
                static_cast<unsigned long long>(stat.calls+1),ms,cpu);
            if(phase==Phase::Update || phase==Phase::Present)
                for(std::size_t i=0;i<frame_ms_.size();++i)
                    if(i!=std::size_t(phase)&&(phase==Phase::Update?
                        (i<std::size_t(Phase::Present)||i>=std::size_t(Phase::Validation)):
                        (i>=std::size_t(Phase::Scene)&&i<std::size_t(Phase::Validation)))&&frame_ms_[i]>0)
                        std::fprintf(stderr," %s=%.3f",names[i],frame_ms_[i]);
            std::fputc('\n',stderr);
            excluded_ms+=std::chrono::duration<double,std::milli>(Clock::now()-log_start).count();
            excluded_cpu_ms+=cpu_ms()-log_cpu;
        }
        stat.total_ms += ms; stat.worst_ms = std::max(stat.worst_ms, ms); ++stat.calls;
        stat.total_cpu_ms+=cpu;stat.worst_cpu_ms=std::max(stat.worst_cpu_ms,cpu);
    }
    void first_present() {
        if (startup_ms_ < 0)
            startup_ms_ = std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
    int depth = 0;
    double excluded_ms = 0; // Probe work, such as world hashes, not game work.
    double excluded_cpu_ms = 0;
private:
    void report() const {
        static const char* names[] = {"update (all)", "  movement", "  combat", "  economy", "  ai",
            "  production", "  tech", "present (all)", "  scene", "  radar", "  hud refresh",
            "  hud draw", "  upload", "  scene state", "  validation", "  orders", "  auto place", "  autoplay"};
        const int scheduler=sched_getscheduler(0);
        const char* policy=scheduler==SCHED_OTHER?"normal":scheduler==SCHED_BATCH?"batch":
            scheduler==SCHED_IDLE?"idle":"other";
        std::fprintf(stderr,"RA2_BENCH scheduler=%s policy=%d\n",policy,scheduler);
        std::fprintf(stderr, "RA2_BENCH startup_to_first_present_ms=%.1f\n", startup_ms_);
        std::fprintf(stderr, "RA2_BENCH %-16s %8s %10s %10s %10s %10s\n", "phase", "calls", "avg_ms", "worst_ms", "avg_cpu", "worst_cpu");
        for (std::size_t i = 0; i < stats_.size(); ++i) {
            const auto& s = stats_[i];
            std::fprintf(stderr, "RA2_BENCH %-16s %8llu %10.3f %10.3f %10.3f %10.3f\n", names[i],
                static_cast<unsigned long long>(s.calls), s.calls ? s.total_ms / double(s.calls) : 0.0, s.worst_ms,
                s.calls?s.total_cpu_ms/double(s.calls):0.0,s.worst_cpu_ms);
        }
    }
    Clock::time_point start_;
    bool enabled_ = false;
    double startup_ms_ = -1;
    std::array<Stat, std::size_t(Phase::Count)> stats_{};
    std::array<double,std::size_t(Phase::Count)> frame_ms_{};
    std::uint64_t frame_=0;
    bool in_frame_=false;
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
        if (outer_) {start_ = Clock::now(); excluded_ = instance().excluded_ms;
            cpu_start_=cpu_ms();excluded_cpu_=instance().excluded_cpu_ms;}
    }
    ~Scope() {
        if (!on_) return;
        --instance().depth;
        if (outer_) instance().add(phase_, std::chrono::duration<double, std::milli>(Clock::now() - start_).count() -
            (instance().excluded_ms - excluded_),cpu_ms()-cpu_start_-(instance().excluded_cpu_ms-excluded_cpu_));
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Phase phase_;
    bool on_, outer_ = false;
    Clock::time_point start_;
    double excluded_ = 0;
    double cpu_start_=0,excluded_cpu_=0;
};

// A top-level or drawing phase. Always records, independent of nesting.
class Timer {
public:
    explicit Timer(Phase phase) : phase_(phase), on_(enabled()) {
        if (on_) {start_ = Clock::now(); excluded_ = instance().excluded_ms;
            cpu_start_=cpu_ms();excluded_cpu_=instance().excluded_cpu_ms;}
    }
    ~Timer() {
        if (on_) instance().add(phase_, std::chrono::duration<double, std::milli>(Clock::now() - start_).count() -
            (instance().excluded_ms - excluded_),cpu_ms()-cpu_start_-(instance().excluded_cpu_ms-excluded_cpu_));
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
private:
    Phase phase_;
    bool on_;
    Clock::time_point start_;
    double excluded_ = 0;
    double cpu_start_=0,excluded_cpu_=0;
};

// Probe-only work inside a timed phase. Its time is removed from every
// enclosing Timer and Scope.
class Exclude {
public:
    Exclude() : on_(enabled()) { if (on_) {start_ = Clock::now();cpu_start_=cpu_ms();} }
    ~Exclude() {
        if (on_) {
            instance().excluded_ms += std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
            instance().excluded_cpu_ms+=cpu_ms()-cpu_start_;
        }
    }
    Exclude(const Exclude&) = delete;
    Exclude& operator=(const Exclude&) = delete;
private:
    bool on_;
    Clock::time_point start_;
    double cpu_start_=0;
};

// Optional first-use detail. Keep its logging outside measured work.
inline void asset_event(const char* kind,const char* type,int first=-1,int second=-1) {
    if(!enabled()||!std::getenv("RA2_BENCH_ASSETS"))return;
    const Exclude logging;
    std::fprintf(stderr,"RA2_BENCH cold %s frame=%llu kind=%s type=%s first=%d second=%d\n",
        instance().in_frame()?"play":"startup",static_cast<unsigned long long>(instance().frame()),
        kind,type,first,second);
}

} // namespace ra2::loop::profile
