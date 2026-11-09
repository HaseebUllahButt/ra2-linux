#include "session/world/setup.h"
#include "sim/prototype_ai.h"
#include "sim/prototype_movement.h"
#include "sim/prototype_production.h"
#include "sim/prototype_economy.h"
#include "sim/prototype_victory.h"
#include "sim/damage_runtime.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
namespace w=ra2::session::world;
namespace s=ra2::sim;
namespace p=s::production;
// Hash explicit values, never object padding or pointers. See README for coverage.
std::uint64_t world_hash(const w::Snapshot& world) {
    std::ostringstream out;
    out.precision(17);
    auto opt=[&](auto v) { out<<bool(v)<<':' ; if(v) out<<+*v; out<<','; };
    out<<int(world.setup_progress)<<world.pending_cell_setup;
    out<<world.random.first<<','<<world.random.second<<',';
    for(auto v:world.random.table) out<<v<<',';
    for(const auto& h:world.houses) {
        out<<h.id<<':'<<h.name<<':'<<h.country;opt(h.cash);opt(h.power_output);opt(h.power_drain);
        opt(h.human_controlled);
    }
    for(const auto& e:world.entities) {
        out<<e.id<<','<<e.house_id<<','<<int(e.kind)<<','<<int(e.lifetime)<<':'<<e.type<<';';
        opt(e.next);opt(e.resolved_hp);opt(e.target_id);opt(e.weapon_timer_start_frame);opt(e.weapon_timer_duration);
        opt(e.mission);opt(e.on_map);opt(e.active);opt(e.in_limbo);opt(e.locomotor_moving);
        if(e.cell) out<<e.cell->x<<','<<e.cell->y;
        if(e.position) out<<e.position->x<<','<<e.position->y<<','<<e.position->z;
    }
    for(const auto& [id,c]:world.cells) {
        out<<id<<';';opt(c.ground_occupancy_124);opt(c.bridge_occupancy_128);opt(c.overlay_data_11e);
        if(c.heads) out<<c.heads->ground_head; // Ground is the prototype's live layer.
    }
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:out.str()) {hash^=c;hash*=1099511628211ull;}
    return hash;
}
struct Result { std::vector<std::uint64_t> hashes; s::Victory winner; };
Result run(w::BaseBattleAssets& assets,const std::string& game,int frames,int replay) {
    w::PrototypeBattleSetup options;
    options.players={{"Computer A","Americans",10000,0,0,2,false},{"Computer B","Russians",10000,1,1,2,false}};
    auto battle=w::prototype_battle(assets,options,s::Random(42).state());
    if(!battle || battle->snapshot.setup_progress!=w::SetupProgress::PrototypeComplete)
        throw std::runtime_error("MP02T2 setup failed");
    auto world=battle->snapshot;
    auto a=world.houses.at(0).id,b=world.houses.at(1).id;
    s::PrototypeAI ai(assets.rules()),bi(assets.rules());
    s::PrototypeMovement movement;
    s::PrototypeEconomy economy(assets.rules());
    s::PrototypeCombatRuntime combat(assets.rules(),assets.battle_rules(),assets.map_ini(),assets.art_ini(),game+"/game.exe");
    auto catalog=p::Catalog::from(assets.rules(),assets.art_ini(),assets.map_ini());
    p::FactorySet factories;
    for(auto house:{a,b}) {
        auto result=p::prototype::initialize(world,factories,catalog,house);
        if(result.status!=p::Status::Done) throw std::runtime_error("production initialize: "+result.detail);
    }
    Result result; s::VictoryState victory;
    std::uint64_t previous=world_hash(world);int unchanged=0;std::size_t fired=0,killed=0,rejected=0,unresolved=0;
    for(int frame=0;frame<frames;++frame) {
        if(frame%100==0) std::cerr<<"run="<<replay<<" frame="<<frame<<'\n';
        for(int side=0;side<2;++side) {
            auto orders=(side?bi:ai).tick(world,side?b:a,side?a:b,frame);
            for(const auto& order:orders) {
                switch(order.kind) {
                case s::skirmish::OrderKind::Deploy:
                    if(p::prototype::deploy(world,factories,catalog,order.entity).status!=p::Status::Done) ++rejected;
                    break;
                case s::skirmish::OrderKind::Build:
                    if(p::prototype::begin_production(world,factories,catalog,order.house,order.type,frame).status!=p::Status::Done) ++rejected;
                    break;
                case s::skirmish::OrderKind::Attack: combat.attack(world,order.entity,order.target);break;
                default: break;
                }
            }
        }
        p::prototype::tick(world,factories,catalog,frame);
        // TODO-ORIGINAL Attack mission: approach a free nearby Cell, then let combat check range.
        auto step=combat.tick(world,frame);fired+=step.fired;killed+=step.destroyed;unresolved+=step.unresolved.size();
        if(!step.unresolved.empty() && unresolved==step.unresolved.size()) std::cout<<"first combat gap: "<<step.unresolved.front()<<'\n';
        for(auto [source,target]:step.out_of_range) {
            if(movement.moving(source)) continue;
            auto actor=std::find_if(world.entities.begin(),world.entities.end(),[&](auto& e){return e.id==source;});
            auto enemy=std::find_if(world.entities.begin(),world.entities.end(),[&](auto& e){return e.id==target;});
            if(actor==world.entities.end() || enemy==world.entities.end() || !enemy->cell) continue;
            int speed=s::original_rule_speed(assets.rules().read_integer(actor->type,"Speed"));
            for(auto delta: {std::pair{2,0},std::pair{0,2},std::pair{-2,0},std::pair{0,-2}}) {
                int x=enemy->cell->x+delta.first,y=enemy->cell->y+delta.second;
                if(x<0 || y<0 || x>=512 || y>=512 || speed<=0) continue;
                if(movement.issue(world,source,{std::uint16_t(x),std::uint16_t(y)},speed).status==s::PrototypeMoveStatus::Complete) break;
            }
        }
        movement.tick(world,frame);economy.tick(world,frame);victory.update(world,a,b);
        if((frame+1)%100==0 || frame+1==frames) {
            if(!w::validate(world)) throw std::runtime_error("invalid shared Cell links at frame "+std::to_string(frame));
            auto hash=world_hash(world);result.hashes.push_back(hash);
            unchanged=hash==previous?unchanged+100:0;previous=hash;
            std::cout<<"run="<<replay<<" frame="<<frame+1<<" hash="<<std::hex<<hash<<std::dec<<" unchanged_frames="<<unchanged<<'\n';
        }
    }
    result.winner=victory.result;
    std::cout<<"run="<<replay<<" winner="<<(victory.result==s::Victory::PlayerWon?"Computer A":victory.result==s::Victory::PlayerLost?"Computer B":victory.result==s::Victory::Draw?"draw":"none (frame limit)")
        <<" fired="<<fired<<" destroyed="<<killed<<" rejected_build_or_deploy="<<rejected<<" combat_gaps="<<unresolved<<'\n';
    return result;
}
int main(int argc,char** argv) try {
    if(argc!=3) throw std::runtime_error("usage: headless_match GAME_DIRECTORY FRAMES");
    int frames=std::stoi(argv[2]);if(frames<100 || frames>1000000) throw std::runtime_error("frames must be 100..1000000");
    auto assets=w::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    auto a=run(assets,argv[1],frames,1),b=run(assets,argv[1],frames,2);
    if(a.hashes!=b.hashes || a.winner!=b.winner) throw std::runtime_error("DESYNC: replay hashes or winner differ");
    std::cout<<"PASS: identical replay checkpoints\n";
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
