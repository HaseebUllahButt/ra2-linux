#pragma once
#include <cstdint>
namespace ra2::sim {
// Expansion 0047B220/0047B2C0 initialize these values through original math lookup code.
constexpr int original_level_height=104;
constexpr int original_bridge_height=416;
// Cell::GetFloorHeight 0047B3A0. This excludes the optional bridge deck height.
int original_floor_height(std::int8_t level,std::uint8_t ramp,std::int32_t world_x,std::int32_t world_y);
}
