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
    // Guard for the original TileType receiver of each Cell. The caller must
    // resolve it again when the receiver changes.
    std::function<std::optional<CellSetupStatic>(std::uint32_t,std::uint32_t)> guard;
    std::map<std::int32_t,float> land_wheels;
};
// Checked base 00472540 call on an allocated Cell, with real ordered Object
// links from Snapshot. No overlay, animation, shadow, or Tube effect is guessed.
// Only Complete publishes to the caller's Snapshot.
CellSetupResult original_live_cell_setup(session::world::Snapshot&,std::uint32_t,const LiveCellSetupInputs&);
}
