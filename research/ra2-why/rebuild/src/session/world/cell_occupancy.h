#pragma once
#include <cstdint>
#include <optional>

namespace ra2::session::world {
// Values at base Cell +124/+128 and +54/+58. Both layers must be resolved.
struct SubcellOccupancy {
    std::uint32_t ground=0, bridge=0;
    std::int32_t ground_owner=-1, bridge_owner=-1;
};
enum class OccupancyLayer { Ground, Bridge };
// Addition checks the bridge flag; removal checks only the height boundary.
std::optional<OccupancyLayer> subcell_layer(bool adding, std::int32_t ground_height,
    std::int32_t threshold, std::int32_t object_height, std::optional<bool> bridge_flag);
// The caller resolves the layer and the original subcell (0 through 4).
bool add_subcell(SubcellOccupancy&, OccupancyLayer, unsigned subcell, std::int32_t owner);
bool remove_subcell(SubcellOccupancy&, OccupancyLayer, unsigned subcell);
// Base Unit 00707980/007079E0 uses bit 20 on the resolved layer.
bool add_unit_occupancy(SubcellOccupancy&, OccupancyLayer);
bool remove_unit_occupancy(SubcellOccupancy&, OccupancyLayer);
// The caller resolves the TerrainType mask and successful unlimbo.
std::optional<std::uint32_t> terrain_occupancy_mask(std::optional<std::int32_t> session_field_838,
    std::optional<std::uint32_t> temperate_mask, std::optional<std::uint32_t> snow_mask);
void add_terrain_occupancy(std::uint32_t& ground, std::uint32_t mask);
void remove_terrain_occupancy(std::uint32_t& ground, std::uint32_t mask);
}
