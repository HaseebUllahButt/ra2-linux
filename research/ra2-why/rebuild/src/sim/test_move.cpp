// Integration test against original map objects and decoded TMP metadata.
// Build with test_move.sh and the parent's current read-only renderer sources.
#include "sim.h"
#include "floor.h"
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
    std::string out=std::to_string(state.frame)+":"+std::to_string(e.position->x)+":"+std::to_string(e.position->y)+":"+std::to_string(e.position->z)+":"+std::to_string(e.moving)+":"+e.mission;
    for (const auto& result:state.results) out+=":"+std::to_string(result.ticket)+":"+std::to_string(static_cast<int>(result.error))+":"+result.detail;
    return out;
}
}
int main(int argc,char** argv) try {
    if (argc<3 || argc>4) throw std::runtime_error("usage: test_move GAME_DIRECTORY EXTRACTED_DIRECTORY [--base]");
    const bool base=argc==4 && std::string(argv[3])=="--base";
    const std::string root=argv[2];
    auto rules=Ini::from_file(root+"/config/"+(base?"rules.ini":"rulesmd.ini"));
    Ini art; art.parse(original_test_art(argv[1],base));
    const std::string executable=std::string(argv[1])+(base?"/game.exe":"/gamemd.exe");
    auto map=Ini::from_file(root+"/maps/C1A02MD.MAP");
    auto terrain=terrain_inputs(original_test_cells(argv[1],root+"/maps/C1A02MD.MAP",base));
    const auto source_catalog=original_test_catalog(argv[1],root+"/maps/C1A02MD.MAP",base);
    TheaterInputs theater; theater.theater_name=source_catalog.name; theater.settings.parse(source_catalog.settings);
    for (const auto& range:source_catalog.ranges) theater.tile_sets.push_back({range.first,range.count});
    theater.resolved_metadata=[&source_catalog](std::uint32_t index,std::uint8_t subtile)->std::optional<TileMetadata> {
        const auto metadata=source_catalog.metadata(index,subtile);
        if (!metadata) return {};
        return TileMetadata{metadata->terrain,metadata->ramp,metadata->valid};
    };
    Simulation a(rules,map,9,terrain,&theater); a.load_ground_assets(executable,art);
    constexpr int directions[][2]={{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}};
    const auto initial=a.snapshot();
    for (const auto& e:initial.entities) if (e.kind==Kind::Unit && e.facing>=0 && e.facing<256 && e.facing%32==0) {
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
        require(e.type=="YTNK" && e.cell.x==113 && e.cell.y==115 && e.facing==0,"real Move fixture changed");
        require(first.types().at(e.type).speed==6,"real YTNK Speed changed");
        // Original 004B0F20 and base 004A1000 at resolved speed 15, relative to their source.
        const int original_y[]={-23,-47,-70,-94,-117,-141,-164,-188,-212,-235,-253,-256};
        Command health; health.op=Op::ResolvedDamage; health.entity=e.id; health.amount=1; health.frame=1;
        require(first.issue(health).error==Error::None && replay.issue(health).error==Error::None,"health guard did not queue");
        unsigned ticks=0;
        while (first.snapshot().entities.at(e.id-1).moving && ticks<1024) {
            first.tick(); replay.tick(); ++ticks;
            require(trace(first.snapshot(),e.id)==trace(replay.snapshot(),e.id),"movement replay mismatch");
            if (ticks==1) require(first.snapshot().results.at(0).error==Error::Unsupported,"unrecovered movement damage was accepted");
            const auto position=first.snapshot().entities.at(e.id-1).position.value();
            require(ticks<=12 && position.x==e.position->x && position.y==e.position->y+original_y[ticks-1],"real map Move differs from original x86 positions");
        }
        const auto final=first.snapshot().entities.at(e.id-1);
        require(ticks==12,"original movement duration changed");
        require(!final.moving && final.position->x==goal.x && final.position->y==goal.y && final.position->z==goal.z,"Move did not reach the original destination");
        require(final.cell.x==move.destination.x && final.cell.y==move.destination.y,"Move cell mismatch");
        require(final.hp==e.hp && final.house==e.house,"Move changed health or owner");
        require(final.mission=="Area Guard","completed single Move did not select the original IQ-based idle mission");
        Command invalid=move; invalid.frame=first.snapshot().frame; invalid.destination.y-=2;
        require(first.issue(invalid).error==Error::None,"unsupported path command did not queue for validation");
        first.tick();
        require(first.snapshot().results.at(0).error==Error::Unsupported,"unrecovered path command was accepted");
        const auto rejected=first.snapshot().entities.at(e.id-1);
        require(rejected.position->x==goal.x && rejected.position->y==goal.y && !rejected.moving,"failed Move changed position");
        std::cout<<"real map Move passed: "<<e.id<<" "<<e.type<<" "<<e.cell.x<<","<<e.cell.y<<" -> "<<move.destination.x<<","<<move.destination.y<<" ticks="<<ticks<<"\n";
        return 0;
    }
    throw std::runtime_error("no real map vehicle completed a supported Move");
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
