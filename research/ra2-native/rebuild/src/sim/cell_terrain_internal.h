#pragma once
// Internal shared terrain decisions. Effect guards and runtime cache writes are separate.
#include "replacement.h"
#include "floor.h"
#include <stdexcept>
namespace ra2::sim::detail {
struct TileTerrainStep { std::optional<std::uint32_t> effect_receiver; };
inline std::string tile_cell_name(const TerrainCell& cell) { return std::to_string(cell.cell.x)+","+std::to_string(cell.cell.y); }
template<class Neighbor>
TileTerrainStep apply_tile_terrain(TerrainCell& cell,const TheaterInputs& theater,
    const TileReplacementRules& rules,std::uint32_t tile_count,std::uint8_t cliff,Neighbor& neighbor) {
    TileTerrainStep step;
    constexpr int cardinal[][2]={{0,-1},{1,0},{0,1},{-1,0}};
    constexpr int cliff_neighbors[][2]={{0,-1},{-1,0},{2,2},{1,1},{-1,1},{1,-1}};
    if (*cell.resolved_tile_index>65535) throw std::invalid_argument("live tile index exceeds the recovered Cell domain");
    if (*cell.resolved_tile_index>=tile_count) cell.resolved_tile_index=0xffff; // 0047D2B0 bounds check.
    const bool no_overlay=*cell.overlay_type_index==-1;
    bool clear=*cell.resolved_tile_index==0xffff;
    if (!clear) {
        auto source=theater.resolved_metadata(*cell.resolved_tile_index,cell.subtile);
        if (!source) throw std::runtime_error("missing original tile metadata at "+tile_cell_name(cell));
        if (!source->valid_subtile) {
            if (!no_overlay) throw std::runtime_error("0047D2B0 overlay-specific invalid subtile branch is not recovered at "+tile_cell_name(cell));
            cell.resolved_tile_index=0xffff; cell.subtile=0; clear=true;
        } else {
            step.effect_receiver=*cell.resolved_tile_index;
            cell.ramp_index=source->ramp;
            TileReplacementInput input; input.index=*cell.resolved_tile_index; input.ramp=source->ramp;
            for (unsigned d=0;d<4;++d) {
                const auto* other=neighbor(cell.cell.x+cardinal[d][0],cell.cell.y+cardinal[d][1]);
                // Map lookup returns the default Cell outside the allocated diamond: 0047BBF0.
                if (!other) input.neighbors[d]=TileNeighbor{0xffff,0};
                else if (other->resolved_tile_index && other->ramp_index)
                    input.neighbors[d]=TileNeighbor{*other->resolved_tile_index,static_cast<std::uint8_t>(*other->ramp_index)};
            }
            const auto decision=original_tile_replacement(input,rules);
            if (!decision.index) throw std::runtime_error(decision.detail+" at "+tile_cell_name(cell));
            cell.resolved_tile_index=*decision.index;
            // 0047D8A3 retains the old TileType receiver after 0047CA80 changes Cell +38.
            // The changed tile supplies terrain only when the next setup pass reads it.
            cell.tmp_terrain=source->terrain;
            cell.land_type=no_overlay?std::optional<std::int32_t>(original_tmp_land_type(source->terrain)):std::nullopt;
        }
    }
    if (clear) { cell.ramp_index=0; cell.tmp_terrain.reset(); cell.land_type=no_overlay?std::optional<std::int32_t>(0):std::nullopt; }
    if (cell.land_type && cliff==2 && (*cell.land_type==0 || *cell.land_type==2 || *cell.land_type==6 || *cell.land_type==8)) {
        for (const auto& delta:cliff_neighbors) {
            const auto* other=neighbor(cell.cell.x+delta[0],cell.cell.y+delta[1]);
            if (other && !other->height_level) throw std::runtime_error("missing original cliff neighbor height");
            const int level=other?*other->height_level:0;
            if (level>=*cell.height_level+4) { cell.land_type=3; break; }
        }
    }
    if (cell.ramp_index && *cell.ramp_index>=0 && *cell.ramp_index<=20)
        cell.floor_world_height=original_floor_height(static_cast<std::int8_t>(*cell.height_level),static_cast<std::uint8_t>(*cell.ramp_index),cell.cell.x*256+128,cell.cell.y*256+128);
    else throw std::runtime_error("original ramp plane is outside 0..20 at "+tile_cell_name(cell));
    cell.tile_replacement_resolved=true;
    // Cell setup preserves bit 100. Bridge setters can change neighboring cells independently of their overlays.
    // Keep a supplied value, including unknown, until the separate bridge phase resolves it.
    return step;
}
}
