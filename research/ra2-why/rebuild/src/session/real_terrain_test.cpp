#include "placement_terrain.h"
#include "../sim/test_inputs.h"
#include "../sim/sim.h"
#include "../sim/world_init.h"
#include <cassert>
#include <iostream>
#include <map>

using namespace ra2;
int main(int argc, char** argv) {
    if (argc<2 || argc>3 || (argc==3 && std::string(argv[2])!="--cases")) return 2;
    const bool cases=argc==3;
    const std::string game=argv[1], map_name="ALL02S.MAP";
    rules::IniFile map, rules;
    map.parse(original_test_ini(game,map_name,true));
    rules.parse(original_test_ini(game,"RULES.INI",true));
    auto rectangle=[&](const char* name) {
        const auto fields=map.read_list("Map",name);
        assert(fields.size()==4);
        std::int32_t value[4];
        for (int i=0;i<4;++i) {
            const auto parsed=rules::IniFile::integer_value(fields[i]); assert(parsed);
            value[i]=*parsed;
        }
        return session::MapRectangle{value[0],value[1],value[2],value[3]};
    };
    const auto geometry=session::resolve_placement_geometry(rectangle("Size"),rectangle("LocalSize"));
    assert(geometry);
    const auto input=original_test_cells(game,map_name,true);
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
        cells.push_back(cell);
    }
    const auto source=original_test_catalog(game,map_name,true);
    sim::TheaterInputs theater;
    theater.theater_name=source.name; theater.settings.parse(source.settings);
    for (const auto range:source.ranges) theater.tile_sets.push_back({range.first,range.count});
    theater.resolved_metadata=[&](std::uint32_t index,std::uint8_t subtile)->std::optional<sim::TileMetadata> {
        const auto m=source.metadata(index,subtile);
        if (!m) return {};
        return sim::TileMetadata{m->terrain,m->ramp,m->valid};
    };
    const auto cliff=static_cast<std::uint8_t>(rules.read_integer("General","CliffBackImpassability",0));
    sim::initialize_original_tile_pass(cells,theater,geometry->width,geometry->height,cliff,true);
    const auto grid=session::PlacementTerrain::create(*geometry,cells); assert(grid);
    sim::Random random(123);
    const auto before=random.state();
    const auto midpoint=cells[cells.size()/2].cell;
    const session::StartPoint start{static_cast<std::int16_t>(midpoint.x),static_cast<std::int16_t>(midpoint.y)};
    const auto unresolved=session::find_initial_placement_checked(start,{0,0,511,511},1,
        session::PlacementEdition::Base,random,
        [&](session::StartPoint point,sim::Random&)->std::optional<bool> {
            return grid->in_map(point,session::PlacementEdition::Base);
        },[](session::StartPoint,sim::Random& state)->std::optional<bool> {
            state.next(); return std::nullopt;
        });
    assert(!unresolved && random.state().table==before.table && random.first()==before.first && random.second()==before.second);
    unsigned known=0,unknown=0;
    for (const auto& cell:cells) {
        const session::StartPoint point{static_cast<std::int16_t>(cell.cell.x),static_cast<std::int16_t>(cell.cell.y)};
        const auto result=grid->in_map(point,session::PlacementEdition::Yuri);
        if (cell.height_level && cell.ramp_index) {
            const auto expected=session::placement_cell_in_map(point,session::PlacementEdition::Yuri,*geometry,
                session::PlacementCell{static_cast<std::int8_t>(*cell.height_level),static_cast<std::uint8_t>(*cell.ramp_index)});
            assert(result==expected); ++known;
            if (cases) {
                const auto raw=rectangle("LocalSize");
                const auto base=grid->in_map(point,session::PlacementEdition::Base);
                assert(base && expected);
                std::cout<<geometry->width<<' '<<geometry->height<<' '
                    <<raw.x<<' '<<raw.y<<' '<<raw.width<<' '<<raw.height<<' '
                    <<geometry->local.x<<' '<<geometry->local.y<<' '<<geometry->local.width<<' '<<geometry->local.height<<' '
                    <<point.x<<' '<<point.y<<' '<<*cell.height_level<<' '<<*cell.ramp_index<<' '
                    <<int(*base)<<' '<<int(*expected)<<'\n';
            }
        } else { assert(!result); ++unknown; }
    }
    assert(known>0);
    if (!cases) std::cout<<map_name<<" first terrain pass: "<<known<<" resolved, "<<unknown<<" unknown cells\n";
}
