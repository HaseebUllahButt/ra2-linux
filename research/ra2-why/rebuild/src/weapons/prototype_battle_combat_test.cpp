#include <session/world/setup.h>
#include <sim/damage_runtime.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace ra2;
namespace world=ra2::session::world;
namespace {
void check(bool value,const char* why) {if (!value) throw std::runtime_error(why);}
world::Entity& by_id(world::Snapshot& state,std::uint32_t id) {
    auto it=std::find_if(state.entities.begin(),state.entities.end(),[id](const auto& e){return e.id==id;});
    if (it==state.entities.end()) throw std::runtime_error("actor missing");
    return *it;
}
void same_rng(const sim::RandomState& a,const sim::RandomState& b) {
    check(a.first==b.first && a.second==b.second && a.table==b.table,"full random replay differs");
}
// Keep the real setup and both starting tanks. Move the target to an empty
// three-Cell test lane so combat can be exercised without a movement order.
void move_target_for_test(world::Snapshot& state,std::uint32_t source_id,std::uint32_t target_id) {
    auto& source=by_id(state,source_id);auto& target=by_id(state,target_id);
    check(source.cell && source.position && target.cell && target.next,"starting tank position is unknown");
    const auto old_index=std::uint32_t(target.cell->y)*512+target.cell->x;
    auto& old_cell=state.cells.at(old_index);
    check(old_cell.heads && old_cell.heads->ground_head==target_id && *target.next==0,
          "starting target has an unexpected shared Cell chain");
    constexpr std::array<std::array<int,2>,4> directions{{{{1,0}},{{0,1}},{{-1,0}},{{0,-1}}}};
    for (const auto& direction:directions) {
        bool clear=true;
        for (int step=1;step<=3;++step) {
            const int x=source.cell->x+direction[0]*step,y=source.cell->y+direction[1]*step;
            if (x<0 || y<0 || x>=512 || y>=512) {clear=false;break;}
            const auto it=state.cells.find(static_cast<std::uint32_t>(y*512+x));
            if (it==state.cells.end() || !it->second.heads || it->second.heads->ground_head ||
                !it->second.terrain || it->second.terrain->overlay_type_index!= -1 ||
                !it->second.terrain->floor_world_height ||
                *it->second.terrain->floor_world_height!=source.position->z) {clear=false;break;}
        }
        if (!clear) continue;
        const int x=source.cell->x+3*direction[0],y=source.cell->y+3*direction[1];
        auto& dest=state.cells.at(static_cast<std::uint32_t>(y*512+x));
        old_cell.heads->ground_head=0;
        // This reposition is a fixture, not recovered Unit movement.
        if (old_cell.ground_occupancy_124) old_cell.ground_occupancy_124=0;
        target.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        target.position=sim::Coord{x*256+128,y*256+128,*dest.terrain->floor_world_height};
        target.collision_virtual_position=target.position;target.next=dest.heads->ground_head;
        dest.heads->ground_head=target.id;
        if (dest.ground_occupancy_124) *dest.ground_occupancy_124|=0x20;
        check(world::validate(state),"repositioned shared world has invalid links");
        return;
    }
    throw std::runtime_error("no clear test lane near the starting tank");
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=2) throw std::runtime_error("usage: prototype_battle_combat_test GAME_DIR");
        auto assets=world::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
        world::PrototypeBattleSetup options;
        options.players={{"Player","Americans",5000,0,0,2,true},
                         {"Enemy","Russians",5000,1,1,2,false}};
        const auto battle=world::prototype_battle(assets,options,sim::Random(42).state());
        check(bool(battle),"actual MP02T2 prototype setup failed");
        auto state=battle->snapshot;
        const auto shooter=std::find_if(state.entities.begin(),state.entities.end(),[&](const auto& e){
            return e.type=="HTNK" && e.house_id==state.houses[1].id && e.lifetime==world::Lifetime::Placed;
        });
        const auto victim=std::find_if(state.entities.begin(),state.entities.end(),[&](const auto& e){
            return e.type=="MTNK" && e.house_id==state.houses[0].id && e.lifetime==world::Lifetime::Placed;
        });
        check(shooter!=state.entities.end() && victim!=state.entities.end(),"starting tanks are missing");
        const auto source_id=shooter->id,target_id=victim->id;
        move_target_for_test(state,source_id,target_id);
        auto replay=state;
        const auto exe=std::string(argv[1])+"/game.exe";
        sim::PrototypeCombatRuntime a(assets.rules(),assets.battle_rules(),assets.map_ini(),assets.art_ini(),exe);
        sim::PrototypeCombatRuntime b(assets.rules(),assets.battle_rules(),assets.map_ini(),assets.art_ini(),exe);
        check(a.attack(state,source_id,target_id) && b.attack(replay,source_id,target_id),"attack was rejected");
        std::size_t fired=0,impacts=0,killed=0;
        for (std::uint64_t frame=0;frame<2000 && !killed;++frame) {
            const auto x=a.tick(state,frame),y=b.tick(replay,frame);
            if (!x.unresolved.empty()) throw std::runtime_error(x.unresolved.front());
            if (!y.unresolved.empty()) throw std::runtime_error(y.unresolved.front());
            check(x.fired==y.fired && x.impacts==y.impacts && x.destroyed==y.destroyed,"replay actions differ");
            const auto& first=by_id(state,target_id);const auto& second=by_id(replay,target_id);
            check(first.resolved_hp==second.resolved_hp && first.lifetime==second.lifetime && first.next==second.next,
                  "target replay differs");
            const auto& af=by_id(state,source_id);const auto& bf=by_id(replay,source_id);
            check(af.weapon_timer_start_frame==bf.weapon_timer_start_frame &&
                  af.weapon_timer_duration==bf.weapon_timer_duration,"reload replay differs");
            same_rng(state.random,replay.random);
            fired+=x.fired;impacts+=x.impacts;killed+=x.destroyed;
        }
        const auto& dead=by_id(state,target_id);
        check(fired>0 && impacts>0 && killed==1 && dead.resolved_hp==0 &&
              dead.lifetime==world::Lifetime::Destroyed,"tank did not die");
        check(world::validate(state) && world::validate(replay),"death broke shared Cell links");
        std::cout<<"MP02T2 starting tanks fight and replay all random words and indexes\n";return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
