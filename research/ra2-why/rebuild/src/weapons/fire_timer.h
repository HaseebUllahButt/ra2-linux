#pragma once
namespace ra2::weapons {
// Techno +244 and +24C. A start of -1 is the original stopped state.
struct FireTimer { int start_frame, duration; };
// Base 006C9690..006C96C2. This is one firing check, not full eligibility.
// Rejects arithmetic overflow and negative durations.
bool base_fire_timer_ready(FireTimer timer, int current_frame);
}

#include <sim/sim.h>
namespace ra2::weapons {
struct SingleShotDelayInput {
    int rate_of_fire; // Original WeaponType +B0.
    double house_multiplier; // Original House +F0.
    bool veteran, elite, veteran_rof, elite_rof;
    double veteran_multiplier; // Original Rules +5B0.
};
// Base 006C9CFE, Unit with one-shot burst and no special weapon effects.
// Advance the shared RNG only after all inputs pass validation. It can draw
// more than one word when the original range reduction retries.
int base_single_shot_unit_delay(const SingleShotDelayInput&, sim::Random&);
}
