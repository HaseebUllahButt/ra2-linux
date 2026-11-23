#include "fire_timer.h"
#include <limits>
#include <stdexcept>
namespace ra2::weapons {
bool base_fire_timer_ready(FireTimer timer, int now) {
    if (timer.duration<0) throw std::invalid_argument("negative fire timer duration");
    if (timer.start_frame==-1) return timer.duration==0;
    const long long elapsed=static_cast<long long>(now)-timer.start_frame;
    if (elapsed<std::numeric_limits<int>::min() || elapsed>std::numeric_limits<int>::max())
        throw std::range_error("fire timer elapsed overflow");
    if (elapsed>=timer.duration) return true;
    const long long remaining=static_cast<long long>(timer.duration)-elapsed;
    if (remaining>std::numeric_limits<int>::max())
        throw std::range_error("fire timer remaining overflow");
    return remaining==0;
}
}

#include <sim/fp.h>
#include <cmath>
namespace ra2::weapons {
int base_single_shot_unit_delay(const SingleShotDelayInput& input, sim::Random& random) {
    if (input.rate_of_fire<0 || !std::isfinite(input.house_multiplier) ||
        input.house_multiplier<=0 || !std::isfinite(input.veteran_multiplier) ||
        input.veteran_multiplier<=0)
        throw std::invalid_argument("invalid firing delay inputs");
    auto trial=random;
    const int draw=trial.ranged(0,2);
    const auto convert=[](double n) {
        if (!std::isfinite(n) || n<std::numeric_limits<int>::min() || n>std::numeric_limits<int>::max())
            throw std::range_error("firing delay conversion overflow");
        return static_cast<int>(n);
    };
    int delay=convert(sim::fp::add(sim::fp::multiply(input.rate_of_fire,input.house_multiplier),draw));
    if ((input.veteran && input.veteran_rof) ||
        (input.elite && (input.veteran_rof || input.elite_rof)))
        delay=convert(sim::fp::multiply(delay,input.veteran_multiplier));
    random=trial;
    return delay;
}
}
