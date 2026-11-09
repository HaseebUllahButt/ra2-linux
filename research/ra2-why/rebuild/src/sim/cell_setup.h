#pragma once
#include "world.h"
#include "coarse_category.h"
namespace ra2::sim {
struct CellSetupState {
    std::optional<bool> allocated; // Distinguish a real Cell from the global fallback receiver.
    TerrainCell terrain;
    std::optional<std::uint8_t> field_11d;
    std::optional<bool> animated_20000;
};
struct CellSetupStatic {
    std::optional<std::uint32_t> receiver_tile_index; // TileType read before replacement.
    std::optional<std::int32_t> animation_index; // Base +298 / expansion +2C8; -1 means absent.
    std::optional<std::uint32_t> animation_subtile; // +2A4 / +2D4.
    std::optional<bool> shadow_enabled; // +2B1 / +2E1.
    std::optional<bool> shadow_list_empty; // Null or an immediate original list terminator.
    std::optional<std::int32_t> bounds_height; // Result of 0052B690 / 00547150, using the old receiver.
};
struct CellSetupNeighbor { bool allocated=false; TerrainCell terrain; };
struct CellSetupCallbacks {
    // Empty is unknown. allocated=false is the original fallback Cell.
    std::function<std::optional<CellSetupNeighbor>(ra2map::Cell)> neighbor;
    // Read-only profile for the final terrain and the current ordered occupants.
    // This function must resolve the matching Land Track value from original rules.
    std::function<std::optional<CoarseCategoryInputs>(const TerrainCell&)> category;
};
enum class CellSetupStatus { Complete, NeedsInput, Unsupported };
struct CellSetupResult {
    CellSetupStatus status=CellSetupStatus::NeedsInput;
    CellSetupState state;
    std::optional<CoarseCategoryResult> category;
    std::optional<std::uint8_t> cache_height; // Coarse record +1 and hierarchy record +8.
    std::string detail;
};
// Base 00472540 / expansion 0047D2B0, argument -1, allocated target,
// no overlay, flat current ramp, and proved absent Tube/animation/shadow effects.
// Reads neighbors without updating them. Only Complete supplies publishable state.
// Errors preserve the input state. Bridge flags, unknown or known, stay unchanged.
CellSetupResult original_overlay_free_cell_setup(const CellSetupState&,const CellSetupStatic&,
    const TheaterInputs&,std::uint8_t cliff_back_impassability,const CellSetupCallbacks&);
}
