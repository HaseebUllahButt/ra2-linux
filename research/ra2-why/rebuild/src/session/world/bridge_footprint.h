#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace ra2::session::world {
struct BridgeCell { std::int16_t x=0, y=0; };
// A resolved original bridge root changes Cell +140 bit 100 at four cells.
// The caller must recover roots and preserve all other Cell flags.
std::optional<std::array<BridgeCell,4>> bridge_flag_cells(BridgeCell root, unsigned direction);
}
