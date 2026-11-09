#include "../placement_terrain.h"
#include "sim/test_inputs.h"
#include "sim/world_init.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>

// Conditional native fixture for MP02T2. Packed Overlay indexes are treated
// as admitted. This does not execute Overlay effects, Cell +11D, Tube or
// animation creation, shadow writes, object loading, or bridge setup.
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    using namespace ra2;
    const std::string game=argv[1];
    rules::IniFile map,rules;
    map.parse(original_test_ini(game,"MP02T2.MAP",true));
    rules.parse(original_test_ini(game,"RULES.INI",true));
    const auto rectangle=[&](const char* name) {
        const auto fields=map.read_list("Map",name);
        assert(fields.size()==4);
        std::int32_t value[4];
        for (int i=0;i<4;++i) {
            const auto parsed=rules::IniFile::integer_value(fields[i]);
            assert(parsed); value[i]=*parsed;
        }
        return session::MapRectangle{value[0],value[1],value[2],value[3]};
    };
    const auto geometry=session::resolve_placement_geometry(rectangle("Size"),rectangle("LocalSize"));
    assert(geometry);
    const auto input=original_test_cells(game,"MP02T2.MAP",true);
    std::map<std::pair<int,int>,const OriginalTestCell*> by_cell;
    for (const auto& cell:input) by_cell[{cell.x,cell.y}]=&cell;
    sim::TerrainCells cells;
    for (const auto coord:sim::original_map_cell_order(geometry->width,geometry->height)) {
        auto cell=sim::original_default_terrain_cell(coord);
        const auto found=by_cell.find({coord.x,coord.y});
        if (found!=by_cell.end()) {
            const auto& src=*found->second;
            cell.tile_index=src.tile; cell.resolved_tile_index=src.resolved;
            cell.subtile=src.subtile; cell.tmp_terrain=src.raw; cell.ramp_index=src.ramp;
            cell.height_level=src.height<128?int(src.height):int(src.height)-256;
            cell.overlay_type_index=src.overlay;
        }
        cell.bridge.reset(); // The omitted bridge phase is unresolved.
        cells.push_back(cell);
    }
    const auto source=original_test_catalog(game,"MP02T2.MAP",true);
    sim::TheaterInputs theater;
    theater.theater_name=source.name; theater.settings.parse(source.settings);
    for (const auto range:source.ranges) theater.tile_sets.push_back({range.first,range.count});
    theater.resolved_metadata=[&](std::uint32_t index,std::uint8_t subtile)->std::optional<sim::TileMetadata> {
        const auto data=source.metadata(index,subtile);
        if (!data) return {};
        return sim::TileMetadata{data->terrain,data->ramp,data->valid};
    };
    const auto cliff=static_cast<std::uint8_t>(map.read_integer("General","CliffBackImpassability",
        rules.read_integer("General","CliffBackImpassability",0)));
    for (int pass=0;pass<2;++pass) {
        sim::initialize_original_tile_pass(cells,theater,geometry->width,geometry->height,cliff,pass==0);
        bool found=false;
        for (const auto& cell:cells) if (cell.cell.x==135 && cell.cell.y==80) {
            assert(cell.resolved_tile_index==131 && cell.tmp_terrain==13 && cell.ramp_index==0);
            assert(cell.land_type==0 && cell.height_level==4 && cell.overlay_type_index==-1);
            assert(!cell.bridge);
            found=true;
        }
        assert(found);
    }
    std::cout<<"MP02T2 conditional tile passes keep waypoint 0 at tile 131, ramp 0, land 0\n";
}
