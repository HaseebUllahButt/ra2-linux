#pragma once
#include <cstdint>

namespace ra2::render {
// Base 0052C4A3 and expansion 00547F63. The original renderer stores
// unsigned 16-bit values. Smaller values are nearer. Terrain accepts ties.
inline std::uint16_t original_tile_depth(std::uint16_t base, std::int32_t origin_y,
        std::int32_t screen_y, std::int8_t level, std::uint8_t pixel_z) {
    const auto bias = static_cast<std::uint16_t>(std::int64_t(base) + origin_y - screen_y - 30);
    return static_cast<std::uint16_t>(std::int32_t(bias) - int(level) * 15 + pixel_z);
}
}
