#pragma once
#include <algorithm>
#include <cstdint>
#include <utility>

namespace ra2::app {
class CameraScroll {
public:
    void reset(std::uint64_t now) {last_=now;remainder_x_=remainder_y_=0;}
    std::pair<int,int> step(std::uint64_t now,int horizontal,int vertical,bool headless=false) {
        // Unlimited headless frames have no elapsed time. Give input checks
        // a fixed minimum interval; visible play always uses elapsed time.
        const auto elapsed=headless?std::max<std::uint64_t>(16,now-last_):
            std::min<std::uint64_t>(100,now-last_);
        last_=now;
        remainder_x_+=elapsed*400;remainder_y_+=elapsed*260;
        const int x=std::clamp(horizontal,-1,1)*int(remainder_x_/1000);
        const int y=std::clamp(vertical,-1,1)*int(remainder_y_/1000);
        remainder_x_%=1000;remainder_y_%=1000;
        return {x,y};
    }
private:
    std::uint64_t last_=0,remainder_x_=0,remainder_y_=0;
};
}
