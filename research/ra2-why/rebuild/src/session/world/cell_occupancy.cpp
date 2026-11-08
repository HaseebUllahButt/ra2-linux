#include "cell_occupancy.h"
#include <limits>

namespace ra2::session::world {
namespace {
bool valid(OccupancyLayer layer, unsigned subcell) {
    return (layer==OccupancyLayer::Ground || layer==OccupancyLayer::Bridge) && subcell<=4;
}
}
std::optional<OccupancyLayer> subcell_layer(bool adding, std::int32_t ground_height,
    std::int32_t threshold, std::int32_t object_height, std::optional<bool> bridge_flag) {
    // Base 00508448 and 005084D3 use signed 32-bit comparisons. Avoid
    // inventing a result when the original addition would overflow.
    const auto boundary=std::int64_t(ground_height)+threshold;
    if (boundary<std::numeric_limits<std::int32_t>::min() ||
        boundary>std::numeric_limits<std::int32_t>::max()) return {};
    if (object_height<boundary) return OccupancyLayer::Ground;
    if (!adding) return OccupancyLayer::Bridge;
    if (!bridge_flag) return {};
    return *bridge_flag?OccupancyLayer::Bridge:OccupancyLayer::Ground;
}
bool add_subcell(SubcellOccupancy& cell, OccupancyLayer layer, unsigned subcell, std::int32_t owner) {
    if (!valid(layer,subcell)) return false;
    // Base 00508410 ORs the subcell bit and stores the object's owner value.
    auto& bits=layer==OccupancyLayer::Ground?cell.ground:cell.bridge;
    auto& field=layer==OccupancyLayer::Ground?cell.ground_owner:cell.bridge_owner;
    bits=static_cast<std::uint8_t>(bits | (1u<<subcell));
    field=owner;
    return true;
}
bool remove_subcell(SubcellOccupancy& cell, OccupancyLayer layer, unsigned subcell) {
    if (!valid(layer,subcell)) return false;
    // Base 005084A0 clears the bit; it resets the owner when bits 2..4 are clear.
    auto& bits=layer==OccupancyLayer::Ground?cell.ground:cell.bridge;
    auto& field=layer==OccupancyLayer::Ground?cell.ground_owner:cell.bridge_owner;
    bits=static_cast<std::uint8_t>(bits & ~(1u<<subcell));
    if (!(bits&0x1c)) field=-1;
    return true;
}
std::optional<std::uint32_t> terrain_occupancy_mask(std::optional<std::int32_t> session_field_838,
    std::optional<std::uint32_t> temperate_mask, std::optional<std::uint32_t> snow_mask) {
    // Base 006E38F6 selects the snow mask only when Scenario +838 equals 1.
    if (!session_field_838) return {};
    return *session_field_838==1?snow_mask:temperate_mask;
}
void add_terrain_occupancy(std::uint8_t& ground, std::uint32_t mask) {
    // Terrain 006E38F0 maps type mask bits 0..2 to Cell +124 bits 2..4.
    ground=static_cast<std::uint8_t>(ground | ((mask&7u)<<2));
}
void remove_terrain_occupancy(std::uint8_t& ground, std::uint32_t mask) {
    // Terrain 006E3850 clears the same bits during removal.
    ground=static_cast<std::uint8_t>(ground & ~((mask&7u)<<2));
}
}
