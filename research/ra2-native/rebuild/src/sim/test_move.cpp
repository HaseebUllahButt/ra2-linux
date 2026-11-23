// Integration test against original map objects and decoded TMP metadata.
// Build with test_move.sh and the parent's current read-only renderer sources.
#include "sim.h"
#include "floor.h"
#include "world_init.h"
#include "test_inputs.h"
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace {
void require(bool value,const std::string& message) { if (!value) throw std::runtime_error(message); }
TerrainCells terrain_inputs(const std::vector<OriginalTestCell>& inputs) {
    TerrainCells cells;
    for (const auto& input:inputs) {
        TerrainCell cell; cell.cell={input.x,input.y}; cell.tile_index=input.tile; cell.resolved_tile_index=input.resolved; cell.subtile=input.subtile;
        cell.tmp_terrain=input.raw; cell.ramp_index=input.ramp;
        const int level=input.height<128?int(input.height):int(input.height)-256;
        cell.height_level=level;
        cell.floor_world_height=original_floor_height(static_cast<std::int8_t>(level),input.ramp,input.x*256+128,input.y*256+128);
        cell.overlay_type_index=input.overlay;
        cells.push_back(cell);
    }
    return cells;
}
std::string trace(const Snapshot& state,std::uint32_t id) {
    const auto& e=state.entities.at(id-1);
    require(e.position.has_value(),"world position missing");
    std::string out=std::to_string(state.frame)+":"+std::to_string(e.position->x)+":"+std::to_string(e.position->y)+":"+std::to_string(e.position->z)+":"+std::to_string(e.moving)+":"+std::to_string(e.facing)+":"+e.mission;
    out+=":"+(e.turret_facing?std::to_string(*e.turret_facing):std::string("unknown-turret"));
    for (const auto& result:state.results) out+=":"+std::to_string(result.ticket)+":"+std::to_string(static_cast<int>(result.error))+":"+result.detail;
    return out;
}
}
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_move GAME_DIRECTORY EXTRACTED_DIRECTORY [--base]");
    const bool base=argc==4 && std::string(argv[3])=="--base";
    const std::string root=argv[2];
    Ini rules; if (base) rules.parse(original_test_ini(argv[1],"RULES.INI",true)); else rules=Ini::from_file(root+"/config/rulesmd.ini");
    Ini art; art.parse(original_test_art(argv[1],base));
    const std::string executable=std::string(argv[1])+(base?"/game.exe":"/gamemd.exe");
    const auto map_source=base?std::string("ALL02S.MAP"):root+"/maps/C1A02MD.MAP";
    Ini map; if (base) map.parse(original_test_ini(argv[1],map_source,true)); else map=Ini::from_file(map_source);
    auto terrain=terrain_inputs(original_test_cells(argv[1],map_source,base));
    const auto source_catalog=original_test_catalog(argv[1],map_source,base);
    TheaterInputs theater; theater.theater_name=source_catalog.name; theater.settings.parse(source_catalog.settings);
    for (const auto& range:source_catalog.ranges) theater.tile_sets.push_back({range.first,range.count});
    theater.resolved_metadata=[&source_catalog](std::uint32_t index,std::uint8_t subtile)->std::optional<TileMetadata> {
        const auto metadata=source_catalog.metadata(index,subtile);
        if (!metadata) return {};
        return TileMetadata{metadata->terrain,metadata->ramp,metadata->valid};
    };
    const auto size=map.read_list("Map","Size");
    auto floor_check=terrain;
    const auto cliff=static_cast<std::uint8_t>(rules.read_integer("General","CliffBackImpassability",0));
    initialize_original_tile_pass(floor_check,theater,*Ini::integer_value(size[2]),*Ini::integer_value(size[3]),cliff,true);
    std::vector<std::optional<int>> first_floors;
    for (const auto& cell:floor_check) first_floors.push_back(cell.floor_world_height);
    initialize_original_tile_pass(floor_check,theater,*Ini::integer_value(size[2]),*Ini::integer_value(size[3]),cliff,false);
    unsigned floor_changes=0;
    for (std::size_t i=0;i<floor_check.size();++i) if (floor_check[i].floor_world_height!=first_floors[i]) ++floor_changes;
    std::cout<<map_source<<" second-pass floor changes="<<floor_changes<<"\n";
    Simulation a(rules,map,9,terrain,&theater); a.load_ground_assets(executable,art);
    constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
    const auto initial=a.snapshot();
    for (const auto& e:initial.entities) if (e.id==(base?60u:259u)) {
        Simulation first(rules,map,9,terrain,&theater); first.load_ground_assets(executable,art);
        Command move; move.op=Op::Move; move.entity=e.id;
        const unsigned direction=static_cast<unsigned>(e.facing)/32;
        move.destination={static_cast<std::uint16_t>(e.cell.x+directions[direction][0]),static_cast<std::uint16_t>(e.cell.y+directions[direction][1])};
        require(first.issue(move).error==Error::None,"Move did not queue"); first.tick();
        const auto result=first.snapshot().results.at(0);
        require(result.error==Error::Unsupported && result.detail=="Move swept-cell bridge state is unresolved","real Move did not reject unknown bridge state");
        const auto after=first.snapshot().entities.at(e.id-1);
        require(e.position && after.position && after.position->x==e.position->x && after.position->y==e.position->y && after.position->z==e.position->z && !after.moving,"unknown bridge state changed the actor");
        require(after.hp==e.hp && after.house==e.house && after.mission==e.mission && after.turret_facing==e.turret_facing,"unknown bridge state changed actor fields");
        Simulation replay(rules,map,9,terrain,&theater); replay.load_ground_assets(executable,art);
        require(replay.issue(move).error==Error::None,"unresolved Move replay did not queue"); replay.tick();
        require(trace(first.snapshot(),e.id)==trace(replay.snapshot(),e.id),"unresolved Move replay differs");
        require(base?(e.type=="HTNK" && e.cell.x==114 && e.cell.y==67):(e.type=="YTNK" && e.cell.x==113 && e.cell.y==115),"real unknown-bridge fixture changed");
        if (base) {
            const auto& routed=initial.entities.at(60);
            require(routed.id==61 && routed.type=="HTNK" && routed.cell.x==117 && routed.cell.y==67,"real route fixture changed");
            for (const auto& path:{std::vector<unsigned>{4,4},std::vector<unsigned>{4,3},std::vector<unsigned>{4,2}}) {
                Simulation route_case(rules,map,9,terrain,&theater); route_case.load_ground_assets(executable,art);
                Command route; route.op=Op::Move; route.entity=routed.id; route.path_directions=path; route.destination=routed.cell;
                for (auto direction:path) { route.destination.x+=directions[direction][0]; route.destination.y+=directions[direction][1]; }
                require(route_case.issue(route).error==Error::None,"unresolved route did not queue"); route_case.tick();
                const auto status=route_case.snapshot().results.at(0);
                require(status.error==Error::Unsupported && status.detail=="Move swept-cell bridge state is unresolved","route accepted unknown bridge state");
                const auto unchanged=route_case.snapshot().entities.at(routed.id-1);
                require(!unchanged.moving && unchanged.position->x==routed.position->x && unchanged.position->y==routed.position->y && unchanged.facing==routed.facing && unchanged.mission==routed.mission && unchanged.turret_facing==routed.turret_facing,"unresolved route changed entity state");
            }
        }
        std::cout<<map_source<<" Move rejects unknown bridge flags; actor state and replay match\n";
        return 0;
    }
    throw std::runtime_error("real map bridge-state fixture is missing");
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
