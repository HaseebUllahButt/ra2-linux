#pragma once
#include "cell_setup.h"
#include <map>
namespace ra2::sim::test {
inline std::uint8_t setup_raw(unsigned index) {
    constexpr std::uint8_t raw[]={0,1,2,3,4,6,7,8,9,10,11,12,13,14,15,0};
    return raw[index%16];
}
inline std::int32_t setup_bounds(unsigned index) {
    const int height=30+int(index%5)*15;
    return index%2?height+int(index%13)-40-(int(index%11)+10):height;
}
struct CellSetupFixture {
    CellSetupState state;
    CellSetupStatic guard;
    TheaterInputs theater;
    CellSetupCallbacks callbacks;
    std::map<std::uint32_t,TerrainCell> neighbors;
    std::uint8_t cliff=0;
    unsigned seed=0;
    bool usable=false,unit=false;
    static std::uint32_t key(ra2map::Cell cell) { return cell.x|(static_cast<std::uint32_t>(cell.y)<<16); }
    explicit CellSetupFixture(unsigned number):seed(number) {
        std::uint32_t value=seed^0xc9185a37;
        const auto next=[&]() { value=value*1664525u+1013904223u; return value>>16; };
        auto& cell=state.terrain; state.allocated=true; cell.cell={20,20};
        cell.tile_index=32+next()%32; cell.resolved_tile_index=cell.tile_index;
        cell.subtile=0; cell.ramp_index=0; cell.overlay_type_index=-1;
        cell.height_level=int(next()%17)-8; cliff=next()%3;
        const auto flags=seed*2654435761u;
        cell.bridge=(flags&0x100)!=0; state.animated_20000=(flags&0x20000)!=0;
        state.field_11d=static_cast<std::uint8_t>(next()); usable=next()%8!=0; (void)next(); unit=true;
        if (seed%8==0) cell.tile_index=0xffff;
        if (seed%8==1) cell.tile_index=96;
        if (seed%8==2) { cell.tile_index=32; cell.subtile=3; }
        cell.resolved_tile_index=cell.tile_index;
        guard.receiver_tile_index=cell.tile_index;
        guard.animation_index=*state.animated_20000 || cell.tile_index%3==0?7:-1;
        guard.animation_subtile=*state.animated_20000?0:1;
        guard.shadow_enabled=cell.tile_index%4==0;
        if (*guard.shadow_enabled) guard.shadow_list_empty=true;
        guard.bounds_height=setup_bounds(cell.tile_index);
        theater.theater_name="TEMPERATE";
        theater.tile_sets={{0,32},{32,16},{48,32}};
        theater.settings.parse("[General]\nRoughTile=1\nClearToRoughLat=2\n");
        theater.resolved_metadata=[](std::uint32_t index,std::uint8_t subtile)->std::optional<TileMetadata> {
            if (index>=80) return {};
            return TileMetadata{setup_raw(index),0,subtile==0};
        };
        constexpr int offsets[][2]={{0,-1},{1,0},{0,1},{-1,0},{2,2},{1,1},{-1,1},{1,-1}};
        for (const auto& delta:offsets) {
            TerrainCell other; other.cell={static_cast<std::uint16_t>(20+delta[0]),static_cast<std::uint16_t>(20+delta[1])};
            constexpr unsigned indexes[]={32,49,7,0xffff};
            other.resolved_tile_index=indexes[next()%4]; other.ramp_index=0;
            other.height_level=*cell.height_level+int(next()%7)-2;
            neighbors.emplace(key(other.cell),other);
        }
        callbacks.neighbor=[&](ra2map::Cell coord)->std::optional<CellSetupNeighbor> {
            const auto found=neighbors.find(key(coord));
            if (found==neighbors.end()) return CellSetupNeighbor{};
            return CellSetupNeighbor{true,found->second};
        };
        callbacks.category=[&](const TerrainCell& terrain)->std::optional<CoarseCategoryInputs> {
            CoarseCategoryInputs input; input.usable=usable;
            input.land_track=(seed+terrain.land_type.value())%3?1.0f:0.0f;
            input.occupants=std::vector<CoarseOccupantState>{};
            if (unit) { CoarseOccupantState object; object.runtime_type=1; input.occupants->push_back(object); }
            return input;
        };
    }
    std::uint32_t digest(const CellSetupResult& out) const {
        std::uint32_t h=2166136261u;
        const auto mix=[&](std::uint32_t value) { h=(h^value)*16777619u; };
        const auto& cell=out.state.terrain;
        mix(*cell.resolved_tile_index); mix(cell.subtile); mix(*cell.ramp_index); mix(*cell.land_type);
        mix(*out.state.field_11d); mix(*cell.bridge); mix(*out.state.animated_20000);
        mix(out.category->category); mix(out.category->writes.size());
        for (auto category:out.category->writes) mix(category);
        mix(*out.cache_height); mix(*out.cache_height);
        return h;
    }
};
}
