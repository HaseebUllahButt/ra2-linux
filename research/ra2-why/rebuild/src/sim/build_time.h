#pragma once
#include <cstdint>

namespace ra2::sim {
enum class BuildEdition { BaseRa2, YurisRevenge };
// Resolved original inputs for base 006C22A0 and expansion 006F47A0.
// Country multipliers and factory count come from the original house helpers.
// This calculation does not establish prerequisites, queues, or placement.
struct BuildTimeInputs {
    BuildEdition edition = BuildEdition::BaseRa2;
    std::int32_t cost = 0;
    double build_speed = 0.7;
    float country_multiplier = 1, type_multiplier = 1;
    std::int32_t power_output = 0, power_drain = 0;
    float minimum_low_power = 0.5f, maximum_low_power = 0.8f, low_power_penalty = 1;
    float multiple_factory = 0.8f;
    std::int32_t factories = 1;
    bool wall = false;
    double wall_coefficient = 3;
};
int original_build_time(const BuildTimeInputs&);
}
