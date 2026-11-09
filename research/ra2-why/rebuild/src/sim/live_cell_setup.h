#pragma once
#include "cell_setup.h"
#include "../session/world/world.h"
#include <map>
namespace ra2::sim {
struct LiveCellSetupInputs {
    const TheaterInputs* theater=nullptr;
    std::optional<std::uint8_t> cliff;
    // Original 512-square Cell pointer array. False is a known null pointer.
    const std::vector<bool>* allocated=nullptr;
    // Explicit current global fallback Cell. Unknown is not a fresh default.
    std::optional<TerrainCell> fallback;
    // Guard for the original TileType receiver of each Cell. The caller must
    // resolve it again when the receiver changes.
    std::function<std::optional<CellSetupStatic>(std::uint32_t,std::uint32_t)> guard;
    std::map<std::int32_t,float> land_wheels;
};
// Base signed Cell lookup. A known null or out-of-range pointer returns
// allocated=false only when its fallback data is explicitly resolved to the
// default values supported by the kernel. Unknown data returns no value.
std::optional<CellSetupNeighbor> original_live_cell_neighbor(const session::world::Snapshot&,
    const std::vector<bool>& allocated,const std::optional<TerrainCell>& fallback,ra2map::Cell);
// Checked base 00472540 call on an allocated Cell, with real ordered Object
// links from Snapshot. No overlay, animation, shadow, or Tube effect is guessed.
// Fallback lookup writes are not represented in Snapshot, so setup rejects
// every potential fallback before calling the kernel. Only Complete publishes.
CellSetupResult original_live_cell_setup(session::world::Snapshot&,std::uint32_t,const LiveCellSetupInputs&);
}
