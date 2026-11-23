#include "prototype_turret.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ra2::sim {
namespace {
constexpr std::int32_t half_facings = facings / 2;

// Wraps any facing into the original 0..255 clockwise scale.
std::int32_t wrap(std::int64_t facing) {
    const auto wrapped = facing % facings;
    return static_cast<std::int32_t>(wrapped < 0 ? wrapped + facings : wrapped);
}

// Clamps the authored ROT rate to a nonnegative per-frame step count.
std::int32_t rot_steps(std::int32_t rot) { return std::max(rot, 0); }
}

std::int32_t facing_difference(std::int32_t from, std::int32_t to) {
    std::int32_t difference = (to - from) % facings;
    if (difference < -half_facings) difference += facings;
    if (difference > half_facings) difference -= facings;
    return difference;
}

std::int32_t aim(std::int32_t current, std::int32_t desired, std::int32_t rot) {
    const auto difference = facing_difference(current, desired);
    const auto magnitude = std::min(std::abs(difference), rot_steps(rot));
    return wrap(current + (difference < 0 ? -magnitude : magnitude));
}

bool ready_to_fire(const FireControl& control) {
    if (control.distance > control.range) return false;
    return control.frame >= control.last_shot + static_cast<std::uint64_t>(std::max(control.rof, 0));
}

std::int32_t direction_to(const Coord& from, const Coord& to) {
    const auto dx = std::int64_t(to.x) - from.x;
    const auto dy = std::int64_t(to.y) - from.y;
    if (dx == 0 && dy == 0) return 0;
    // 0 = north, 64 = east, 128 = south, 192 = west on the 256-step circle.
    const auto angle = std::atan2(double(dx), -double(dy));
    auto facing = std::lround(angle * (half_facings / 3.14159265358979323846));
    return static_cast<std::int32_t>(wrap(facing));
}

std::int64_t lepton_distance(const Coord& from, const Coord& to) {
    const auto dx = std::int64_t(to.x) - from.x;
    const auto dy = std::int64_t(to.y) - from.y;
    const auto dz = std::int64_t(to.z) - from.z;
    return std::llround(std::sqrt(double(dx * dx + dy * dy + dz * dz)));
}
}
