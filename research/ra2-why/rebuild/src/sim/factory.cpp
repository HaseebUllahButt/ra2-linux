#include "sim.h"
#include <algorithm>
#include <stdexcept>

namespace ra2::sim {
void Factory::begin(int cost,int frames,std::uint64_t frame) {
    if (cost<0 || frames<0) throw std::invalid_argument("negative resolved factory inputs");
    // 004C9C70 resets progress; 004C9EA0 starts the interval timer.
    progress=0; remaining_cost=cost; interval=factory_stage_frames(frames);
    timer_start=frame; suspended=false; insufficient_funds=false;
}
void Factory::tick(House& house,std::uint64_t frame) {
    if (suspended || done()) return;
    if (frame<timer_start) throw std::invalid_argument("factory frame precedes timer");
    if (frame-timer_start<static_cast<unsigned>(interval)) return; // 00426630.
    if (!house.cash) throw std::runtime_error("factory needs resolved cash and no stored ore");
    // 004C9B78..004C9C03: increment first, then spread remaining cost.
    ++progress; timer_start=frame;
    const int divisor=54-progress;
    const int installment=std::min(remaining_cost,divisor?remaining_cost/divisor:remaining_cost);
    if (installment>*house.cash) {
        insufficient_funds=true; --progress; return;
    }
    insufficient_funds=false;
    *house.cash-=installment; house.spent+=installment; remaining_cost-=installment;
    if (done()) suspended=true;
}
}
