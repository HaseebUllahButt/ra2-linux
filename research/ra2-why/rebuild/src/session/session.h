#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim { class Random; class OriginalDistance; }
namespace ra2::session {
// The multiplayer house constructor copies these values before later setup.
struct HouseValues { std::int32_t initial_credits, cash, color, house_type_color; };
HouseValues initialize_house_values(std::int32_t credits, std::int32_t color) noexcept;
// Candidate order follows the resolved General/BaseUnit list. An unknown Owner
// mask blocks selection because it could be the first matching candidate.
struct BaseUnitCandidate { std::string type; std::optional<std::uint32_t> owner_mask; };
std::optional<std::size_t> select_base_unit(const std::vector<BaseUnitCandidate>& candidates,
                                            unsigned country_index);
// Original waypoint cell coordinates are signed 16-bit values.
struct StartPoint { std::int16_t x, y; };
// Inclusive cell bounds and resolved map and object checks. The validation
// callback runs for every Yuri retry, including the origin. Base runs it only
// for the initial point. The placement callback resolves occupancy and unit
// placement. Both may consume the same RNG. Use only when checks are resolved.
struct PlacementBounds { std::int16_t left, top, right, bottom; };
enum class PlacementEdition { Base, Yuri };
using PlacementCheck = std::function<bool(StartPoint, sim::Random&)>;
std::optional<StartPoint> find_initial_placement(StartPoint origin, PlacementBounds bounds,
    PlacementEdition edition, sim::Random& random, const PlacementCheck& valid, const PlacementCheck& place);
// Returns a point index and marks it used. The RNG is the simulation's shared
// original state. Invalid or unsupported input leaves both arguments unchanged.
// This covers regular players in standard Battle mode, without observers.
// It is not a complete skirmish initialization plan.
std::optional<std::size_t> choose_start_point(const std::vector<StartPoint>& points,
    std::vector<bool>& used, sim::Random& random,
    const sim::OriginalDistance& distance);
}
