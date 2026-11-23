#include "overlay_cell_setup.h"
#include <stdexcept>
namespace ra2::session::world {
sim::CellSetupResult setup_base_resource_overlay_cell(const sim::CellSetupState& input,
    const sim::CellSetupStatic& guard,const sim::TheaterInputs& theater,std::uint8_t cliff,
    const sim::CellSetupCallbacks& callbacks,std::optional<std::int32_t> overlay_land,
    std::optional<bool> no_use_tile_land,std::optional<bool> resource_match) {
    sim::CellSetupResult missing;
    missing.state=input;
    missing.detail="resource Overlay Cell inputs are unresolved";
    if (!input.terrain.overlay_type_index || *input.terrain.overlay_type_index<0 || !overlay_land ||
        !no_use_tile_land || !resource_match || !input.terrain.resolved_tile_index || !callbacks.category)
        return missing;
    if (*overlay_land!=5 || *no_use_tile_land || !*resource_match) {
        missing.status=sim::CellSetupStatus::Unsupported;
        missing.detail="Overlay Cell is outside the checked resource path";
        return missing;
    }
    std::uint32_t tile_count=0;
    for (const auto& set:theater.tile_sets) {
        if (set.first!=tile_count || set.count>65535 || tile_count+set.count>65536) return missing;
        tile_count+=set.count;
    }
    const auto tile=*input.terrain.resolved_tile_index;
    if (tile==0xffff || tile>=tile_count || !theater.resolved_metadata) return missing;
    std::optional<sim::TileMetadata> metadata;
    try { metadata=theater.resolved_metadata(tile,input.terrain.subtile); }
    catch (const std::runtime_error& error) { missing.detail=error.what(); return missing; }
    if (!metadata || !metadata->valid_subtile || metadata->ramp!=0) return missing;
    // The checked flat, valid tile path has the same replacement and effect
    // guards as an overlay-free Cell. Resolve those without publishing them.
    auto base=input;
    base.terrain.overlay_type_index=-1;
    auto checked_callbacks=callbacks;
    // A fallback lookup can mutate the shared fallback Cell coordinate.
    // This slice does not yet return those writes, so reject that path.
    checked_callbacks.neighbor=[&](ra2map::Cell point)->std::optional<sim::CellSetupNeighbor> {
        if (!callbacks.neighbor) return {};
        const auto neighbor=callbacks.neighbor(point);
        if (!neighbor || !neighbor->allocated) return {};
        return neighbor;
    };
    checked_callbacks.category=[](const sim::TerrainCell&) -> std::optional<sim::CoarseCategoryInputs> {
        sim::CoarseCategoryInputs value; value.usable=false; return value;
    };
    auto result=sim::original_overlay_free_cell_setup(base,guard,theater,cliff,checked_callbacks);
    if (result.status!=sim::CellSetupStatus::Complete) { result.state=input; return result; }
    // 00472540 starts with Overlay Land 5. A recognized resource on a flat
    // tile keeps that land through the later resource branch.
    result.state.terrain.overlay_type_index=input.terrain.overlay_type_index;
    result.state.terrain.land_type=5;
    const auto category=callbacks.category(result.state.terrain);
    if (!category) return missing;
    try {
        auto resolved=*category;
        resolved.overlay_index=input.terrain.overlay_type_index;
        resolved.land_type=5;
        result.category=sim::original_coarse_category(resolved);
    } catch (const std::runtime_error& error) {
        missing.detail=error.what(); return missing;
    } catch (const std::invalid_argument& error) {
        missing.detail=error.what(); return missing;
    }
    return result;
}
}
