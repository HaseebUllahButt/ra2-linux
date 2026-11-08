#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra2::sim { class Random; class OriginalDistance; }
namespace ra2::session {
// Original waypoint cell coordinates are signed 16-bit values.
struct StartPoint { std::int16_t x, y; };
// Returns a point index and marks it used. The RNG is the simulation's shared
// original state. Invalid or unsupported input leaves both arguments unchanged.
// This covers regular players in standard Battle mode, without observers.
// It is not a complete skirmish initialization plan.
std::optional<std::size_t> choose_start_point(const std::vector<StartPoint>& points,
    std::vector<bool>& used, sim::Random& random,
    const sim::OriginalDistance& distance);
}
