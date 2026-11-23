#include "sim/prototype_movement.h"
#include "save_game.h"
#include <cstdio>
#include "../session/world/setup.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool v,const char* text) { if (!v) throw std::runtime_error(text); }
std::uint32_t key(int x,int y) {return std::uint32_t(y)*512+std::uint32_t(x);}
}
std::uint64_t hash(const sw::Snapshot& world) {
    session::save::SavedGame state;state.world=world;
    std::uint64_t h=14695981039346656037ull;
    for(unsigned char c:session::save::encode(state)){h^=c;h*=1099511628211ull;}return h;
}
int main(int argc,char** argv) try {
    if (argc!=3) throw std::runtime_error("usage: test_prototype_movement_real game_directory");
    auto assets=sw::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    sw::PrototypeBattleSetup options;
    options.players={{"Player","Americans",5000,0,0,2,true},{"Enemy","Russians",5000,1,1,2,false}};
    const auto battle=sw::prototype_battle(assets,options,Random(42).state());
    require(battle && battle->snapshot.setup_progress==sw::SetupProgress::PrototypeComplete && battle->snapshot.cells.size()==19900,
            "MP02T2 prototype Battle setup failed");
    auto a=battle->snapshot,b=battle->snapshot;PrototypeMovement one,two;
    std::vector<std::uint32_t> ids;std::vector<ra2map::Cell> goals;
    constexpr int offsets[][2]={{3,0},{0,3},{-3,0},{0,-3},{2,2},{-2,2},{2,-2},{-2,-2}};
    for (const auto& unit:battle->snapshot.entities) {
        if (ids.size()==2) break;
        if (unit.house_id!=battle->snapshot.houses[0].id || unit.kind!=sw::Kind::Unit || !unit.cell || !unit.resolved_hp) continue;
        const auto* value=assets.rules().find(unit.type,"Speed");
        const auto authored=value?rules::IniFile::integer_value(value->value):std::nullopt;
        require(authored.has_value(),"original Unit speed is absent from installed rules");
        const auto speed=original_rule_speed(*authored);bool issued=false;
        for (const auto& delta:offsets) {
            const int x=int(unit.cell->x)+delta[0],y=int(unit.cell->y)+delta[1];
            if (x<0 || y<0 || x>=512 || y>=512) continue;
            const ra2map::Cell target{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
            if (std::any_of(goals.begin(),goals.end(),[&](ra2map::Cell c){return c.x==target.x && c.y==target.y;})) continue;
            if (one.issue(a,unit.id,target,speed).status!=PrototypeMoveStatus::Complete) continue;
            require(two.issue(b,unit.id,target,speed).status==PrototypeMoveStatus::Complete,"real-map replay command failed");
            ids.push_back(unit.id);goals.push_back(target);issued=true;break;
        }
        require(issued,"no nearby real-map prototype goal was found");
    }
    require(ids.size()==2,"prototype setup lacks two human vehicles");
    // Controllers stay live: their private route import API belongs to the movement owner.
    unsigned frame=0;bool concurrent=false;
    session::save::SavedGame saved;saved.world=b;
    saved.factories.flags.player_house=b.houses[0].id;
    saved.factories.factories.push_back({b.houses[0].id,3,7,"HTNK",24,800,3,17,7,false});
    saved.factories.house_factory_counts[b.houses[0].id]={1,2,3,4,5,6};
    saved.factories.power_dirty[1]=true;saved.factories.power_blackout_idle[1]=false;
    saved.factories.buildings[1]={true,true,5};
    saved.moves[ids[0]]={goals[0],64,{goals[0],goals[1]}};
    saved.movement_last_frame=3;
    const auto bytes=session::save::encode(saved);
    session::save::SavedGame decoded;
    require(session::save::decode(bytes,decoded) && session::save::encode(decoded)==bytes,"full state round trip failed");
    require(decoded.factories.factories[0].timer_duration==7 &&
        decoded.factories.factories[0].interval==3 && !decoded.factories.factories[0].suspended,
        "running factory timer did not survive save");
    const auto unchanged=session::save::encode(decoded);
    for(std::size_t n: {std::size_t(0),std::size_t(9),bytes.size()-1})
        require(!session::save::decode(bytes.substr(0,n),decoded),"truncated input accepted");
    auto bad=bytes;bad[11]^=1;
    require(!session::save::decode(bad,decoded),"wrong version accepted");
    bad=bytes;bad[100]^=1;
    require(!session::save::decode(bad,decoded),"corruption accepted");
    require(!session::save::decode(bytes+"x",decoded),"trailing data accepted");
    require(session::save::encode(decoded)==unchanged,"failed decode changed output");
    require(!session::save::load_file("/nonexistent/ra2-save"),"missing file accepted");
    while (one.active_count() && frame<500) {
        const auto step=one.tick(a,frame),replay=two.tick(b,frame);
        if (step.status!=PrototypeMoveStatus::Complete) std::cerr<<step.detail<<'\n';
        require(step.status==PrototypeMoveStatus::Complete && replay.status==PrototypeMoveStatus::Complete,"prototype real-map frame failed");
        concurrent|=step.moved.size()>=2;
        for (const auto id:ids) {
            const auto left=std::find_if(a.entities.begin(),a.entities.end(),[&](const auto& e){return e.id==id;});
            const auto right=std::find_if(b.entities.begin(),b.entities.end(),[&](const auto& e){return e.id==id;});
            require(left!=a.entities.end() && right!=b.entities.end() && left->position && right->position &&
                left->position->x==right->position->x && left->position->y==right->position->y && left->position->z==right->position->z &&
                left->cell->x==right->cell->x && left->cell->y==right->cell->y && left->next==right->next,
                "prototype real-map replay differs");
        }
        require(a.random.table==b.random.table && a.random.first==b.random.first &&
                a.random.second==b.random.second,"prototype real-map movement changed RNG");
        // Consume the shared RNG after each frame, including after restore.
        Random left_rng(a.random),right_rng(b.random);
        require(left_rng.next()==right_rng.next(),"restored RNG sequence differs");
        a.random=left_rng.state();b.random=right_rng.state();
        ++frame;
        if(frame==5) {
            saved.world=b;saved.frame=frame;
            const std::string path=std::string(argv[2])+"/battle.sav";
            require(session::save::save_file(path,saved),"save failed");
            auto loaded=session::save::load_file(path);std::remove(path.c_str());
            require(loaded && session::save::encode(*loaded)==session::save::encode(saved),"file round trip failed");
            b=std::move(loaded->world);
        }
        require(hash(a)==hash(b),"saved world hash differs after frame");
    }
    require(frame<500 && concurrent && !two.active_count(),"prototype real-map Units did not arrive together");
    for (unsigned i=0;i<ids.size();++i) {
        const auto unit=std::find_if(a.entities.begin(),a.entities.end(),[&](const auto& e){return e.id==ids[i];});
        const auto& c=a.cells.at(key(goals[i].x,goals[i].y));
        require(unit!=a.entities.end() && unit->cell->x==goals[i].x && unit->cell->y==goals[i].y &&
                c.heads->ground_head==unit->id && *c.ground_occupancy_124==32,"prototype real-map Cell links differ");
    }
    std::cout<<"MP02T2 prototype Battle: 2 installed Units moved and replayed in "<<frame<<" shared frames\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
