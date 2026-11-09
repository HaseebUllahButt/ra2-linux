#include "world.h"
#include <array>
#include <map>
#include <stdexcept>
namespace ra2::sim {
TerrainCell original_default_terrain_cell(ra2map::Cell coord) {
    TerrainCell out; out.cell=coord; out.tile_index=0xffff; out.resolved_tile_index=0xffff;
    out.subtile=0; out.height_level=0; out.ramp_index=0; out.floor_world_height=0;
    out.overlay_type_index=-1; out.land_type=0; out.bridge=false;
    return out;
}
int original_tmp_land_type(std::uint8_t raw) {
    // 00544BE0, table at 008288E4. TMP terrain bytes are not Cell LandType values.
    constexpr int land[]={0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3};
    if (raw>=16) throw std::out_of_range("00544BE0 raw TMP terrain byte exceeds the original conversion table");
    return land[raw];
}
void resolve_overlay_free_terrain(TerrainCells& cells,std::uint8_t cliff) {
    using Key=std::pair<int,int>;
    std::map<Key,const TerrainCell*> by_cell;
    for (const auto& cell:cells) by_cell.emplace(Key{cell.cell.x,cell.cell.y},&cell);
    // 0047D2B0's exact six-cell height tests, in original short-circuit order.
    constexpr int neighbors[][2]={{0,-1},{-1,0},{2,2},{1,1},{-1,1},{1,-1}};
    for (auto& cell:cells) {
        if (cell.land_type || !cell.tmp_terrain || !cell.overlay_type_index || *cell.overlay_type_index!=-1) continue;
        const int mapped=original_tmp_land_type(*cell.tmp_terrain);
        int final=mapped;
        const bool can_change=cliff==2 && (mapped==0 || mapped==2 || mapped==6 || mapped==8);
        if (can_change) {
            if (!cell.height_level) continue;
            bool high_neighbor=false, unknown=false;
            for (const auto& delta:neighbors) {
                const auto it=by_cell.find({static_cast<int>(cell.cell.x)+delta[0],static_cast<int>(cell.cell.y)+delta[1]});
                if (it==by_cell.end() || !it->second->height_level) { unknown=true; continue; }
                if (static_cast<std::int64_t>(*it->second->height_level)>=static_cast<std::int64_t>(*cell.height_level)+4) { high_neighbor=true; break; }
            }
            if (high_neighbor) final=3;
            else if (unknown) continue;
        }
        cell.land_type=final;
        // Preserve the explicit Cell +140 bit 100 state. Neighbor bridge setters can affect clear cells.
    }
}
}
