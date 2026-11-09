#pragma once
#include "sim/cell_setup.h"
namespace ra2::session::world {
// Base 00472540 slice for a valid, flat tile with an Overlay that is a
// resolved resource. Overlay Land must be Tiberium. Other Overlay paths,
// Tube creation, animations, and shadow writes remain separate.
sim::CellSetupResult setup_base_resource_overlay_cell(const sim::CellSetupState&,
    const sim::CellSetupStatic&,const sim::TheaterInputs&,std::uint8_t cliff,
    const sim::CellSetupCallbacks&,std::optional<std::int32_t> overlay_land,
    std::optional<bool> no_use_tile_land,std::optional<bool> resource_lookup_matches);
}
