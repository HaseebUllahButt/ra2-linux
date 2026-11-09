#include "live_cell_setup.h"
namespace ra2::sim {
CellSetupResult original_live_cell_setup(session::world::Snapshot& world,std::uint32_t key,const LiveCellSetupInputs& in) {
    CellSetupResult missing;missing.detail="live Cell setup input is unresolved";
    if (world.edition!=session::world::Edition::Base || world.setup_progress!=session::world::SetupProgress::Complete || world.pending_cell_setup || world.pending_setup_cell ||
        !in.theater || !in.cliff || !in.allocated || in.allocated->size()!=512u*512u || key>=in.allocated->size() ||
        !(*in.allocated)[key] || !in.guard) return missing;
    const auto found=world.cells.find(key);
    if (found==world.cells.end() || !found->second.allocated || !*found->second.allocated || !found->second.terrain ||
        !found->second.terrain->resolved_tile_index || !found->second.cell_flags_140 || !found->second.cell_flag_20000)
        return missing;
    const auto guard=in.guard(key,*found->second.terrain->resolved_tile_index);
    if (!guard) return missing;
    CellSetupState input;input.allocated=found->second.allocated;input.terrain=*found->second.terrain;
    input.field_11d=found->second.cell_field_11d;input.animated_20000=found->second.cell_flag_20000;
    CellSetupCallbacks callbacks;
    callbacks.neighbor=[&](ra2map::Cell point)->std::optional<CellSetupNeighbor>{
        const auto index=std::uint32_t(point.y)*512+point.x;
        if (index>=in.allocated->size()) return {};
        if (!(*in.allocated)[index]) return CellSetupNeighbor{};
        const auto cell=world.cells.find(index);
        if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated || !cell->second.terrain) return {};
        return CellSetupNeighbor{true,*cell->second.terrain};
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
