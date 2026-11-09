#include "prototype_production.h"
#include "session/world/setup.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace ra2;
namespace w=ra2::session::world;
namespace p=ra2::sim::production;
namespace proto=ra2::sim::production::prototype;

namespace {
void require(bool condition,const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void done(const p::Outcome& result,const std::string& operation) {
    if (result.status!=p::Status::Done)
        throw std::runtime_error(operation+": "+result.detail+" (status "+std::to_string(int(result.status))+")");
}
w::Entity& actor(w::Snapshot& world,std::uint32_t id) {
    for (auto& item:world.entities) if (item.id==id) return item;
    throw std::runtime_error("missing actor "+std::to_string(id));
}
p::FactoryState* factory(p::FactorySet& factories,std::uint32_t id) {
    for (auto& item:factories.factories) if (item.object==id) return &item;
    return nullptr;
}
void require_prototype(const w::Snapshot& world) {
    require(world.setup_progress==w::SetupProgress::PrototypeComplete && w::validate(world),
            "production changed the prototype phase or links");
}

std::uint32_t build(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,
                    std::uint32_t house,const std::string& type,std::uint64_t& frame,bool full_run,
                    bool block_exit=false) {
    const auto outcome=proto::begin_production(world,factories,catalog,house,type,frame);
    done(outcome,"begin "+type);
    require(outcome.entity.has_value(),"factory object is missing");
    const auto id=*outcome.entity;
    auto* state=factory(factories,id);
    require(state && state->interval>0,"factory is not running");
    if (!full_run) state->progress=53; // Test the completion boundary without waiting for 53 stages.
    std::vector<std::pair<std::uint32_t,std::uint32_t>> saved;
    while (state && state->progress<54) {
        if (block_exit && state->progress==53 && saved.empty()) {
            for (auto& item:world.cells) if (item.second.ground_occupancy_124) {
                saved.push_back({item.first,*item.second.ground_occupancy_124});
                *item.second.ground_occupancy_124|=0x20;
            }
        }
        frame=std::max(frame+1,state->timer_start+std::uint64_t(state->interval));
        done(proto::tick(world,factories,catalog,frame),"tick "+type);
        require_prototype(world);
        state=factory(factories,id);
        if (block_exit && state && state->progress==54) {
            require(actor(world,id).lifetime==w::Lifetime::Created,"blocked exit released an actor");
            const auto* first_cell=&world.cells.begin()->second;
            done(proto::tick(world,factories,catalog,++frame),"wait for exit "+type);
            require(first_cell==&world.cells.begin()->second && factory(factories,id) &&
                    factory(factories,id)->progress==54,"blocked exit replaced the world or lost its actor");
            for (const auto& item:saved) world.cells.at(item.first).ground_occupancy_124=item.second;
            done(proto::tick(world,factories,catalog,++frame),"retry exit "+type);
            require_prototype(world);
            state=factory(factories,id);
            break;
        }
    }
    return id;
}

void place_building(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,
                    std::uint32_t house,const std::string& type,std::uint64_t frame,ra2map::Cell base) {
    const auto* facts=catalog.find(type);
    require(facts,"missing building rules for "+type);
    const auto& offsets=p::original_foundation_cells(facts->placement.foundation);
    require(!offsets.empty(),"missing building footprint for "+type);
    for (int radius=1;radius<=20;++radius) for (int dy=-radius;dy<=radius;++dy)
        for (int dx=-radius;dx<=radius;++dx) {
            if (std::max(std::abs(dx),std::abs(dy))!=radius) continue;
            const int x=int(base.x)+dx,y=int(base.y)+dy;
            if (x<0 || y<0 || x>=512 || y>=512) continue;
            const p::CellXY origin{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
            if (proto::passes_proximity(world,factories,catalog,house,type,origin)!=true) continue;
            bool clear=true;
            for (const auto& offset:offsets) {
                const int xx=x+offset.x,yy=y+offset.y;
                if (xx<0 || yy<0 || xx>=512 || yy>=512) { clear=false; break; }
                const auto found=world.cells.find(std::uint32_t(yy)*512+xx);
                if (found==world.cells.end() || !found->second.heads || found->second.heads->ground_head ||
                    !found->second.ground_occupancy_124 || *found->second.ground_occupancy_124 ||
                    !found->second.terrain || found->second.terrain->overlay_type_index!= -1 ||
                    found->second.terrain->ramp_index!=0) { clear=false; break; }
            }
            if (!clear) continue;
            const auto outcome=proto::place(world,factories,catalog,house,type,origin,frame);
            if (outcome.status==p::Status::Done) { require_prototype(world); return; }
        }
    throw std::runtime_error("no place for "+type);
}
}

int main(int argc,char** argv) try {
    require(argc==2,"usage: test_prototype_production GAME_DIRECTORY");
    auto assets=w::BaseBattleAssets::load(argv[1],"MP02T2.MAP");
    w::PrototypeBattleSetup setup;
    setup.players={{"Player","Americans",20000,0,0,2,true},{"Enemy","Russians",20000,1,1,2,false}};
    auto loaded=w::prototype_battle(assets,setup,sim::Random(42).state());
    require(loaded.has_value(),"cannot load MP02T2 prototype");
    auto& world=loaded->snapshot;
    const auto player=world.houses[0].id, enemy=world.houses[1].id;
    const auto find_unit=[&](std::uint32_t house,const std::string& type) {
        for (const auto& item:world.entities) if (item.house_id==house && item.type==type) return item.id;
        throw std::runtime_error("missing starting "+type);
    };
    const auto amcv=find_unit(player,"AMCV"),smcv=find_unit(enemy,"SMCV");
    const auto catalog=p::Catalog::from(assets.rules(),assets.art_ini(),assets.map_ini());
    require(catalog.find("GACNST") && catalog.find("NACNST") &&
            catalog.find("GACNST")->placement.foundation==18 &&
            catalog.find("NACNST")->placement.foundation==18,"installed yard foundations differ");
    p::FactorySet factories;
    {
        auto wrong=world; wrong.setup_progress=w::SetupProgress::Complete;
        require(proto::initialize(wrong,factories,catalog,player).status==p::Status::NeedsInput,
                "prototype adapter accepted an exact world");
    }
    done(proto::initialize(world,factories,catalog,player),"initialize");
    require_prototype(world);
    const auto old_random=world.random;
    const auto* first_cell=&world.cells.begin()->second;
    done(proto::tick(world,factories,catalog,0),"idle tick");
    require(first_cell==&world.cells.begin()->second && world.random.table==old_random.table &&
            world.random.first==old_random.first && world.random.second==old_random.second,
            "idle tick replaced the world or changed the random state");
    require(proto::deploy(world,factories,catalog,amcv,false).status==p::Status::Rejected &&
            actor(world,amcv).lifetime==w::Lifetime::Placed && world.random.table==old_random.table,
            "rejected deployment changed the world");
    // A prototype turn completes immediately, so a moved MCV can still deploy.
    actor(world,amcv).hull_facing=0; actor(world,amcv).hull_facing_raw=0;
    const auto allied=proto::deploy(world,factories,catalog,amcv,true);
    done(allied,"deploy AMCV");
    const auto soviet=proto::deploy(world,factories,catalog,smcv,true);
    done(soviet,"deploy AI SMCV");
    require(allied.entity && soviet.entity && actor(world,*allied.entity).type=="GACNST" &&
            actor(world,*soviet.entity).type=="NACNST","deployed yard types differ");
    require(world.houses[0].human_controlled==true && world.houses[1].human_controlled==false,
            "AI deployment changed House control");
    require(actor(world,amcv).lifetime==w::Lifetime::Destroyed && actor(world,amcv).active==false &&
            actor(world,*allied.entity).active==true && actor(world,*soviet.entity).active==true,
            "deploy did not update live actors");
    require(world.random.first!=old_random.first || world.random.second!=old_random.second,
            "deploy did not preserve construction draws");
    require_prototype(world);
    const auto base=*actor(world,*allied.entity).cell;
    const auto enemy_base=*actor(world,*soviet.entity).cell;
    std::uint64_t frame=0;
    done(proto::tick(world,factories,catalog,frame),"initial power tick");

    const auto cash=*world.houses[0].cash;
    build(world,factories,catalog,player,"GAPOWR",frame,true);
    require(*world.houses[0].cash==cash-catalog.find("GAPOWR")->cost,"plant cost differs");
    place_building(world,factories,catalog,player,"GAPOWR",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"power update");
    require(*world.houses[0].power_output>0,"plant produced no power");
    build(world,factories,catalog,player,"GAPILE",frame,false);
    place_building(world,factories,catalog,player,"GAPILE",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"barracks power update");
    const auto infantry=build(world,factories,catalog,player,"E1",frame,false,true);
    require(!factory(factories,infantry),"completed infantry stayed in its factory");
    require(actor(world,infantry).kind==w::Kind::Infantry && actor(world,infantry).active==true &&
            actor(world,infantry).in_limbo==false && actor(world,infantry).cell,
            "infantry did not leave the barracks");
    const auto foot=*actor(world,infantry).cell;
    require((*world.cells.at(std::uint32_t(foot.y)*512+foot.x).ground_occupancy_124&0x20)!=0,
            "infantry exit did not reserve its Cell");

    build(world,factories,catalog,player,"GAREFN",frame,false);
    place_building(world,factories,catalog,player,"GAREFN",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"refinery power update");
    build(world,factories,catalog,player,"GAWEAP",frame,false);
    place_building(world,factories,catalog,player,"GAWEAP",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"factory power update");
    const auto tank=build(world,factories,catalog,player,"MTNK",frame,false);
    require(!factory(factories,tank) && actor(world,tank).kind==w::Kind::Unit &&
            actor(world,tank).lifetime==w::Lifetime::Placed && actor(world,tank).resolved_hp>0 &&
            actor(world,tank).active==true && actor(world,tank).next,
            "tank did not leave the war factory");
    build(world,factories,catalog,enemy,"NAPOWR",frame,false);
    place_building(world,factories,catalog,enemy,"NAPOWR",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI power update");
    build(world,factories,catalog,enemy,"NAHAND",frame,false);
    place_building(world,factories,catalog,enemy,"NAHAND",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI barracks power update");
    const auto enemy_infantry=build(world,factories,catalog,enemy,"E2",frame,false);
    require(!factory(factories,enemy_infantry) && actor(world,enemy_infantry).house_id==enemy &&
            actor(world,enemy_infantry).lifetime==w::Lifetime::Placed && actor(world,enemy_infantry).active==true,
            "AI infantry did not leave the barracks");
    build(world,factories,catalog,enemy,"NAREFN",frame,false);
    place_building(world,factories,catalog,enemy,"NAREFN",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI refinery power update");
    build(world,factories,catalog,enemy,"NAWEAP",frame,false);
    place_building(world,factories,catalog,enemy,"NAWEAP",frame,enemy_base);
    done(proto::tick(world,factories,catalog,++frame),"AI factory power update");
    const auto enemy_tank=build(world,factories,catalog,enemy,"HTNK",frame,false);
    require(!factory(factories,enemy_tank) && actor(world,enemy_tank).house_id==enemy &&
            actor(world,enemy_tank).lifetime==w::Lifetime::Placed && actor(world,enemy_tank).active==true,
            "AI tank did not leave the war factory");
    require_prototype(world);
    std::cout<<"MP02T2 prototype deploys both Houses and produces infantry and a tank\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
