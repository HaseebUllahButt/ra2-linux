#include "live_cell_setup.h"
namespace ra2::sim {
namespace {
bool supported_fallback(const std::optional<TerrainCell>& fallback) {
    return fallback && fallback->resolved_tile_index==0xffffu && fallback->ramp_index==0 && fallback->height_level==0;
}
}
std::optional<CellSetupNeighbor> original_live_cell_neighbor(const session::world::Snapshot& world,
    const std::vector<bool>& allocated,const std::optional<TerrainCell>& fallback,ra2map::Cell point) {
    if (allocated.size()!=512u*512u) return {};
    const auto signed_word=[](std::uint16_t value){return value<0x8000u?int(value):int(value)-0x10000;};
    const auto offset=std::int64_t(signed_word(point.y))*512+signed_word(point.x);
    if (offset<0 || offset>=512*512) return supported_fallback(fallback)?std::optional<CellSetupNeighbor>(CellSetupNeighbor{}):std::nullopt;
    const auto index=static_cast<std::uint32_t>(offset);
    if (!allocated[index]) return supported_fallback(fallback)?std::optional<CellSetupNeighbor>(CellSetupNeighbor{}):std::nullopt;
    const auto cell=world.cells.find(index);
    if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated || !cell->second.terrain) return {};
    return CellSetupNeighbor{true,*cell->second.terrain};
}
CellSetupResult original_live_cell_setup(session::world::Snapshot& world,std::uint32_t key,const LiveCellSetupInputs& in) {
    CellSetupResult missing;missing.detail="live Cell setup input is unresolved";
    if (world.edition!=session::world::Edition::Base || world.setup_progress!=session::world::SetupProgress::Complete || world.pending_cell_setup || world.pending_setup_cell ||
        !in.theater || !in.cliff || !in.allocated || in.allocated->size()!=512u*512u || key>=in.allocated->size() ||
        !(*in.allocated)[key] || !in.guard) return missing;
    const auto found=world.cells.find(key);
    if (found==world.cells.end() || !found->second.allocated || !*found->second.allocated || !found->second.terrain ||
        !found->second.terrain->resolved_tile_index || !found->second.cell_flags_140 || !found->second.cell_flag_20000)
        return missing;
    // The kernel handles out-of-range offsets internally as fallback Cells.
    // Preflight every potential cardinal and cliff lookup before calling it.
    constexpr int offsets[][2]={{0,-1},{1,0},{0,1},{-1,0},{2,2},{1,1},{-1,1},{1,-1}};
    for (const auto& delta:offsets) {
        const auto index=(std::int64_t(key/512)+delta[1])*512+std::int64_t(key%512)+delta[0];
        if (index<0 || index>=512*512 || !(*in.allocated)[static_cast<std::size_t>(index)]) {
            // Original fallback lookup writes the global fallback coordinate.
            // Snapshot does not yet carry that write, even when its terrain is known.
            missing.detail="Cell setup fallback coordinate write is unresolved";return missing;
        }
    }
    const auto guard=in.guard(key,*found->second.terrain->resolved_tile_index);
    if (!guard) return missing;
    CellSetupState input;input.allocated=found->second.allocated;input.terrain=*found->second.terrain;
    input.field_11d=found->second.cell_field_11d;input.animated_20000=found->second.cell_flag_20000;
    CellSetupCallbacks callbacks;
    callbacks.neighbor=[&](ra2map::Cell point) {
        return original_live_cell_neighbor(world,*in.allocated,in.fallback,point);
    };
    callbacks.category=[&](const TerrainCell& terrain)->std::optional<CoarseCategoryInputs>{
        auto category=session::world::coarse_inputs(world,key);
        if (!category) return {};
        category->overlay_index=terrain.overlay_type_index;category->land_type=terrain.land_type;category->land_wheel.reset();
        if (terrain.land_type) {
            const auto wheel=in.land_wheels.find(*terrain.land_type);
            if (wheel!=in.land_wheels.end()) category->land_wheel=wheel->second;
        }
        return category;
    };
    auto result=original_overlay_free_cell_setup(input,*guard,*in.theater,*in.cliff,callbacks);
    if (result.status!=CellSetupStatus::Complete) return result;
    if (!result.category || !result.cache_height || !result.state.animated_20000 || !result.state.field_11d) return missing;
    auto candidate=world;auto& cell=candidate.cells.at(key);
    if (!cell.terrain->land_type || !result.state.terrain.land_type || *cell.terrain->land_type!=*result.state.terrain.land_type)
        cell.unit_land_speed.reset();
    cell.terrain=result.state.terrain;cell.cell_field_11d=result.state.field_11d;cell.cell_flag_20000=result.state.animated_20000;
    cell.coarse_category=result.category->category;cell.coarse_category_writes=result.category->writes;
    cell.coarse_cache_height=*result.cache_height;cell.hierarchy_cache_height=*result.cache_height;
    cell.land_wheel.reset();
    if (cell.terrain->land_type) {
        const auto wheel=in.land_wheels.find(*cell.terrain->land_type);
        if (wheel!=in.land_wheels.end()) cell.land_wheel=wheel->second;
    }
    if (*cell.cell_flag_20000) *cell.cell_flags_140|=0x20000u;else *cell.cell_flags_140&=~0x20000u;
    if (!session::world::validate(candidate)) { missing.detail="live Cell setup produced invalid flags or links";return missing; }
    world=std::move(candidate);return result;
}
}
