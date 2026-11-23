#include "build_time.h"
#include "fp.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ra2::sim {
namespace {
int integer(double value) {
    if (!std::isfinite(value) || value < 0 || value > std::numeric_limits<int>::max())
        throw std::range_error("Build time exceeds the verified original integer domain");
    return static_cast<int>(value);
}
float single_zero(double value) {
    float result = static_cast<float>(value);
    if (std::abs(double(result)) > std::abs(value)) result = std::nextafter(result, 0.0f);
    return result;
}
}
int original_build_time(const BuildTimeInputs& input) {
    if (input.cost < 0 || input.power_output < 0 || input.power_drain < 0 ||
        input.factories < 0 || input.factories > 1024 ||
        (input.edition != BuildEdition::BaseRa2 && input.edition != BuildEdition::YurisRevenge))
        throw std::invalid_argument("Invalid original build-time inputs");
    for (double value : {input.build_speed, double(input.country_multiplier), double(input.type_multiplier),
                         double(input.minimum_low_power), double(input.maximum_low_power),
                         double(input.low_power_penalty), double(input.multiple_factory), input.wall_coefficient})
        if (!std::isfinite(value) || value < 0 || value > 100)
            throw std::invalid_argument("Build-time multiplier exceeds the verified original domain");
    // TechnoType virtual +88: base 006DAD10, expansion 00711EE0.
    int frames = integer(fp::multiply(fp::multiply(input.cost, input.build_speed), 0.9));
    frames = integer(fp::multiply(frames, input.country_multiplier));
    // Base RA2 has no expansion BuildTimeMultiplier step.
    if (input.edition == BuildEdition::YurisRevenge)
        frames = integer(fp::multiply(frames, input.type_multiplier));
    float power = 1;
    if (input.power_output < input.power_drain && input.power_drain != 0)
        power = input.power_output == 0 ? 0.0f :
            single_zero(fp::divide(input.power_output, input.power_drain));
    double factor = fp::add(1, -fp::multiply(fp::add(1, -double(power)), input.low_power_penalty));
    if (factor <= input.minimum_low_power) factor = input.minimum_low_power;
    if (power < 1 && factor >= input.maximum_low_power) factor = input.maximum_low_power;
    if (factor == 0) factor = double(0.01f);
    frames = integer(fp::divide(frames, factor));
    if (input.multiple_factory > 0)
        for (int count = 1; count < input.factories; ++count)
            frames = integer(fp::multiply(frames, input.multiple_factory));
    if (input.wall) frames = integer(fp::multiply(frames, input.wall_coefficient));
    return frames;
}
}
