#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>

namespace ra2::app {
// Held-arrow camera travel. Speed starts slow and rises while the key stays
// held, so short taps move the view a little (QA4: 300-500 ms holds jumped).
// Travel follows wall-clock time, so frame rate and game speed do not change it.
class CameraScroll {
public:
    static constexpr std::uint64_t start_speed=60;  // pixels per second at key down
    static constexpr std::uint64_t top_speed=240;   // pixels per second after the ramp
    static constexpr std::uint64_t ramp_ms=2000;    // time to reach top speed
    static constexpr std::uint64_t max_step_ms=100; // a stall never jumps the view

    // Horizontal pixels after holding one arrow for held_ms.
    static std::uint64_t travel(std::uint64_t held_ms) {
        const auto ramp=std::min(held_ms,ramp_ms);
        const auto milli=start_speed*ramp+(top_speed-start_speed)*ramp*ramp/(2*ramp_ms)+
            top_speed*(held_ms-ramp);
        return milli/1000;
    }
    // Vertical travel keeps the old 130:200 ratio for the isometric view.
    static std::uint64_t travel_vertical(std::uint64_t held_ms) {return travel(held_ms)*13/20;}

    void reset(std::uint64_t now) {last_=now;x_={};y_={};}
    std::pair<int,int> step(std::uint64_t now,int horizontal,int vertical,bool headless=false) {
        // Unlimited headless frames have no elapsed time. Give input checks
        // a fixed minimum interval; visible play always uses elapsed time.
        const auto since=now>=last_?now-last_:0;
        const auto elapsed=std::min(max_step_ms,headless?std::max<std::uint64_t>(16,since):since);
        last_=now;
        return {x_.advance(horizontal,elapsed,travel),y_.advance(vertical,elapsed,travel_vertical)};
    }
private:
    struct Axis {
        int direction=0;std::uint64_t held=0,moved=0;
        int advance(int input,std::uint64_t elapsed,std::uint64_t(*distance)(std::uint64_t)) {
            input=std::clamp(input,-1,1);
            // Release or reverse starts the gentle ramp again.
            if(input!=direction){direction=input;held=moved=0;}
            if(!input)return 0;
            held+=elapsed;
            const auto total=distance(held);
            const int delta=int(total-moved);moved=total;
            return input*delta;
        }
    };
    std::uint64_t last_=0;Axis x_,y_;
};
}
