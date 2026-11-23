#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace ra2::session::world {
struct BridgeCell { std::int16_t x=0, y=0; };
// A resolved original bridge root changes Cell +140 bit 100 at four cells.
// The caller must recover roots and preserve all other Cell flags.
std::optional<std::array<BridgeCell,4>> bridge_flag_cells(BridgeCell root, unsigned direction);
// Base Overlay 005DA970. A value identifies a recognised root. No value
// does not prove that an overlay or later phase cannot create another root.
std::optional<unsigned> overlay_bridge_direction(std::int32_t type_index,
    std::uint8_t cell_ramp, std::int32_t placement_mode, bool placement_allowed);
}
