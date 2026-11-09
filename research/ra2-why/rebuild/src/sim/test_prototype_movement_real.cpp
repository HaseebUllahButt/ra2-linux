#include "prototype_movement.h"
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
int main(int argc,char** argv) try {
    if (argc!=2) throw std::runtime_error("usage: test_prototype_movement_real game_directory");
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
    unsigned frame=0;bool concurrent=false;
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
        require(a.random.table==battle->snapshot.random.table && a.random.first==battle->snapshot.random.first &&
                a.random.second==battle->snapshot.random.second,"prototype real-map movement changed RNG");
        ++frame;
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
