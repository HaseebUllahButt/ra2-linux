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
        if (result.error!=Error::None) { std::cout<<e.id<<" "<<e.type<<" "<<e.cell.x<<","<<e.cell.y<<" rejected: "<<result.detail<<"\n"; continue; }
        Simulation replay(rules,map,9,terrain,&theater); replay.load_ground_assets(executable,art);
        require(replay.issue(move).error==Error::None,"replay did not queue"); replay.tick();
        require(trace(first.snapshot(),e.id)==trace(replay.snapshot(),e.id),"dispatch replay mismatch");
        const auto goal=Coord{move.destination.x*256+128,move.destination.y*256+128,e.position->z};
        require(base?(e.type=="HTNK" && e.cell.x==114 && e.cell.y==67 && e.facing==128):(e.type=="YTNK" && e.cell.x==113 && e.cell.y==115 && e.facing==0),"real Move fixture changed");
        require(e.turret_facing==e.facing,"original map turret facing differs");
        require(first.types().at(e.type).speed==6,"real vehicle Speed changed");
        // Original 004B0F20 and base 004A1000 at resolved speed 15, relative to their source.
        const int original_y[]={23,47,70,94,117,141,164,188,212,235,253,256};
        Command health; health.op=Op::ResolvedDamage; health.entity=e.id; health.amount=1; health.frame=1;
        require(first.issue(health).error==Error::None && replay.issue(health).error==Error::None,"health guard did not queue");
        unsigned ticks=0;
        while (first.snapshot().entities.at(e.id-1).moving && ticks<1024) {
            first.tick(); replay.tick(); ++ticks;
            require(trace(first.snapshot(),e.id)==trace(replay.snapshot(),e.id),"movement replay mismatch");
            if (ticks==1) require(first.snapshot().results.at(0).error==Error::Unsupported,"unrecovered movement damage was accepted");
            const auto position=first.snapshot().entities.at(e.id-1).position.value();
            require(ticks<=12 && position.x==e.position->x && position.y==e.position->y+directions[direction][1]*original_y[ticks-1],"real map Move differs from original x86 positions");
        }
        const auto final=first.snapshot().entities.at(e.id-1);
        require(ticks==12,"original movement duration changed");
        require(!final.moving && final.position->x==goal.x && final.position->y==goal.y && final.position->z==goal.z,"Move did not reach the original destination");
        require(final.cell.x==move.destination.x && final.cell.y==move.destination.y,"Move cell mismatch");
        require(final.turret_facing==e.turret_facing,"straight Move changed the separate turret pose");
        require(final.hp==e.hp && final.house==e.house,"Move changed health or owner");
        const auto expected_mission="Area Guard";
        std::cout<<"original IQ="<<map.read_integer(e.house,"IQ",0)<<" GuardArea="<<rules.read_integer("IQ","GuardArea",4)<<"\n";
        if (final.mission!=expected_mission) std::cout<<"actual idle mission "<<final.mission<<" owner="<<e.house<<"\n";
        require(final.mission==expected_mission,"completed single Move did not select the original IQ-based idle mission");
        for (const auto& cell:first.terrain()) if (cell.cell.x==e.cell.x && (cell.cell.y==e.cell.y || cell.cell.y==move.destination.y))
            std::cout<<"fixture cell "<<cell.cell.x<<","<<cell.cell.y<<" land="<<cell.land_type.value_or(-1)<<"\n";
        if (base) {
            const auto& routed=initial.entities.at(60);
            require(routed.id==61 && routed.type=="HTNK" && routed.cell.x==117 && routed.cell.y==67 && routed.facing==128,"real route fixture changed");
            struct RealRoute { std::vector<unsigned> path; std::vector<DrivePoint> points; };
            const RealRoute route_vectors[]={
#include "real_route_golden.inc"
            };
            for (const auto& golden:route_vectors) {
                Simulation route_case(rules,map,9,a.terrain()),route_replay(rules,map,9,a.terrain());
                route_case.load_ground_assets(executable,art); route_replay.load_ground_assets(executable,art);
                Command route; route.op=Op::Move; route.entity=routed.id; route.path_directions=golden.path;
                route.destination=routed.cell;
                for (auto direction:golden.path) { route.destination.x+=directions[direction][0]; route.destination.y+=directions[direction][1]; }
                require(route_case.issue(route).error==Error::None && route_replay.issue(route).error==Error::None,"real route did not queue");
                route_case.tick(); route_replay.tick();
                require(route_case.snapshot().results.at(0).error==Error::None,"real route rejected");
                require(trace(route_case.snapshot(),routed.id)==trace(route_replay.snapshot(),routed.id),"real route dispatch replay differs");
                for (std::size_t step=0;step<golden.points.size();++step) {
                    route_case.tick(); route_replay.tick();
                    const auto actual=route_case.snapshot().entities.at(routed.id-1);
                    const auto expected=golden.points[step];
                    if (golden.path[0]==golden.path[1]) require(actual.turret_facing==routed.turret_facing,"straight route changed the separate turret pose");
                    else require(!actual.turret_facing,"turn invented a turret pose from the hull facing");
                    require(actual.position->x==routed.position->x+expected.x && actual.position->y==routed.position->y+expected.y && actual.position->z==routed.position->z && actual.facing==expected.facing,"real route position or facing differs from original x86");
                    require(actual.moving==(step+1<golden.points.size()),"real route duration differs from original x86");
                    require(trace(route_case.snapshot(),routed.id)==trace(route_replay.snapshot(),routed.id),"real route replay differs");
                }
                const auto final_route=route_case.snapshot().entities.at(routed.id-1);
                require(final_route.cell.x==route.destination.x && final_route.cell.y==route.destination.y && final_route.mission==expected_mission && final_route.hp==routed.hp,"real route completion state differs");
                std::cout<<"real base route passed: entity 61 path="<<golden.path[0]<<","<<golden.path[1]<<" ticks="<<golden.points.size()<<"\n";
            }
            for (const auto& path:{std::vector<unsigned>{4,5},std::vector<unsigned>{4,8},std::vector<unsigned>{4,3,2}}) {
                Simulation rejected_case(rules,map,9,a.terrain()); rejected_case.load_ground_assets(executable,art);
                Command rejected_route; rejected_route.op=Op::Move; rejected_route.entity=routed.id; rejected_route.path_directions=path;
                rejected_route.destination=path==std::vector<unsigned>{4,5}?ra2map::Cell{116,69}:ra2map::Cell{118,69};
                require(rejected_case.issue(rejected_route).error==Error::None,"rejected route did not queue"); rejected_case.tick();
                require(rejected_case.snapshot().results.at(0).error==Error::Unsupported,"invalid or unrecovered route was accepted");
                if (path==std::vector<unsigned>{4,5}) require(rejected_case.snapshot().results.at(0).detail.find("resolved clear or road")!=std::string::npos,"real curve terrain guard was not reached");
                const auto unchanged=rejected_case.snapshot().entities.at(routed.id-1);
                require(!unchanged.moving && unchanged.position->x==routed.position->x && unchanged.position->y==routed.position->y && unchanged.facing==routed.facing && unchanged.mission==routed.mission && unchanged.turret_facing==routed.turret_facing,"rejected route changed entity state");
            }
            bool moved_on_clear=false;
            for (const auto& candidate:initial.entities) {
                if (candidate.kind!=Kind::Unit || candidate.type!="HTNK" || candidate.house!=e.house || !candidate.position || candidate.facing<0 || candidate.facing>=256 || candidate.facing%64) continue;
                const unsigned facing=static_cast<unsigned>(candidate.facing)/32;
                const ra2map::Cell target{static_cast<std::uint16_t>(candidate.cell.x+directions[facing][0]),static_cast<std::uint16_t>(candidate.cell.y+directions[facing][1])};
                bool source_clear=false,target_clear=false;
                for (const auto& cell:a.terrain()) {
                    const bool supported=cell.land_type==0 && cell.ramp_index==0 && cell.bridge==false && cell.overlay_type_index==-1 && cell.tile_replacement_resolved;
                    if (cell.cell.x==candidate.cell.x && cell.cell.y==candidate.cell.y) source_clear=supported;
                    if (cell.cell.x==target.x && cell.cell.y==target.y) target_clear=supported;
                }
                if (!source_clear || !target_clear) continue;
                require(floor_changes==0,"real base clear fixture needs first-pass placement floors");
                Simulation clear_case(rules,map,9,a.terrain()),clear_replay(rules,map,9,a.terrain());
                clear_case.load_ground_assets(executable,art); clear_replay.load_ground_assets(executable,art);
                Command next; next.op=Op::Move; next.entity=candidate.id; next.destination=target;
                require(clear_case.issue(next).error==Error::None && clear_replay.issue(next).error==Error::None,"base clear command did not queue");
                clear_case.tick(); clear_replay.tick();
                const auto status=clear_case.snapshot().results.at(0);
                if (status.error!=Error::None) { std::cout<<"clear candidate "<<candidate.id<<" rejected: "<<status.detail<<"\n"; continue; }
                for (unsigned step=0;step<12;++step) {
                    clear_case.tick(); clear_replay.tick();
                    require(trace(clear_case.snapshot(),candidate.id)==trace(clear_replay.snapshot(),candidate.id),"base clear movement replay differs");
                    const auto actual=clear_case.snapshot().entities.at(candidate.id-1);
                    require(actual.position->x==candidate.position->x+directions[facing][0]*original_y[step] && actual.position->y==candidate.position->y+directions[facing][1]*original_y[step] && actual.position->z==candidate.position->z,"base clear movement differs from original x86 positions");
                    require(actual.moving==(step<11),"base clear movement duration differs");
                }
                moved_on_clear=true;
                std::cout<<"real base clear Move passed: entity "<<candidate.id<<" "<<candidate.cell.x<<","<<candidate.cell.y<<" -> "<<target.x<<","<<target.y<<"\n";
                break;
            }
            require(moved_on_clear,"real base map has no completed supported clear-cell Move");
        }
        const auto last_completed=first.snapshot().entities.at(e.id-1);
        Command invalid=move; invalid.frame=first.snapshot().frame; invalid.destination.y-=2;
        require(first.issue(invalid).error==Error::None,"unsupported path command did not queue for validation");
        first.tick();
        require(first.snapshot().results.at(0).error==Error::Unsupported,"unrecovered path command was accepted");
        const auto rejected=first.snapshot().entities.at(e.id-1);
        require(rejected.position->x==last_completed.position->x && rejected.position->y==last_completed.position->y && !rejected.moving,"failed Move changed position");
        std::cout<<"real map Move passed: "<<e.id<<" "<<e.type<<" "<<e.cell.x<<","<<e.cell.y<<" -> "<<move.destination.x<<","<<move.destination.y<<" ticks="<<ticks<<"\n";
        return 0;
    }
    throw std::runtime_error("no real map vehicle completed a supported Move");
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
