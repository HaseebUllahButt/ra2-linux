#include "prototype_production.h"
#include "build_time.h"
#include "sim.h"
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
void destroy_actor(w::Snapshot& world,std::uint32_t id) {
    auto& item=actor(world,id);
    require(item.next==0,"test actor has a nonempty shared link");
    for (auto& [key,cell]:world.cells) {
        (void)key;
        if (!cell.heads) continue;
        if (cell.heads->ground_head==id) {
            cell.heads->ground_head=0;
            if ((item.kind==w::Kind::Unit || item.kind==w::Kind::Infantry) && cell.ground_occupancy_124)
                *cell.ground_occupancy_124&=~0x20u;
        }
        if (cell.heads->bridge_head==id) cell.heads->bridge_head=0;
    }
    item.lifetime=w::Lifetime::Destroyed; item.resolved_hp=0; item.active=false; item.in_limbo=true;
    item.on_map=false; item.next=0; item.object_field_74=0; item.object_field_76=1;
}

int count_at(const std::optional<std::map<std::int32_t,std::int32_t>>& counts,std::int32_t index) {
    require(counts.has_value(),"counter is unknown");
    const auto item=counts->find(index);
    return item==counts->end() ? 0 : item->second;
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
        frame=std::max(frame+1,state->timer_start+std::uint64_t(state->timer_duration));
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

p::CellXY place_building(w::Snapshot& world,p::FactorySet& factories,const p::Catalog& catalog,
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
            if (outcome.status==p::Status::Done) { require_prototype(world); return origin; }
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
    sim::Random expected(old_random);
    expected.next(); expected.next();
    require(world.random.table==expected.state().table && world.random.first==expected.first() &&
            world.random.second==expected.second(),"deployed Houses did not share the same random stream");
    require_prototype(world);
    const auto base=*actor(world,*allied.entity).cell;
    const auto enemy_base=*actor(world,*soviet.entity).cell;
    std::uint64_t frame=0;
    done(proto::tick(world,factories,catalog,frame),"initial power tick");

    // A queued building must not keep the House alive after its last yard dies.
    {
        auto lost=world;
        auto lost_factories=factories;
        const auto before_cash=*lost.houses[0].cash;
        const auto started=proto::begin_production(lost,lost_factories,catalog,player,"GAPOWR",frame);
        done(started,"queue plant before yard loss");
        require(started.entity.has_value(),"queued plant is missing");
        auto* pending=factory(lost_factories,*started.entity);
        require(pending,"queued plant factory is missing");
        const auto paid_frame=frame+pending->timer_duration;
        done(proto::tick(lost,lost_factories,catalog,paid_frame),"pay one plant stage");
        require(*lost.houses[0].cash<before_cash,"plant stage did not debit cash");
        std::vector<std::uint32_t> victims;
        for (const auto& item:lost.entities)
            if (item.house_id==player && item.lifetime==w::Lifetime::Placed) victims.push_back(item.id);
        for (const auto id:victims) destroy_actor(lost,id);
        const auto random_before_abandon=lost.random;
        done(proto::tick(lost,lost_factories,catalog,paid_frame+1),"abandon after yard loss");
        require(actor(lost,*started.entity).lifetime==w::Lifetime::Destroyed &&
                !factory(lost_factories,*started.entity) && !lost_factories.buildings.count(*allied.entity) &&
                !lost_factories.buildings.count(*started.entity),"yard loss kept its queued building");
        require(*lost.houses[0].cash==before_cash && lost.houses[0].unit_count==0 &&
                count_at(lost.houses[0].buildings_owned_5420,catalog.find("GAPOWR")->heap_index)==0 &&
                count_at(lost.houses[0].buildings_active_5460,catalog.find("GACNST")->heap_index)==0 &&
                lost_factories.house_factory_counts.at(player)[3]==0,
                "yard loss kept stale money or counters");
        require(lost.random.table==random_before_abandon.table && lost.random.first==random_before_abandon.first &&
                lost.random.second==random_before_abandon.second,"abandon changed the shared random state");
        require(std::none_of(lost.entities.begin(),lost.entities.end(),[&](const auto& item){
            return item.house_id==player && item.lifetime!=w::Lifetime::Destroyed &&
                (item.kind==w::Kind::Unit || item.kind==w::Kind::Infantry || item.kind==w::Kind::Structure);
        }),"queued building still prevents defeat");
        require_prototype(lost);
    }

    const auto cash=*world.houses[0].cash;
    build(world,factories,catalog,player,"GAPOWR",frame,true);
    require(*world.houses[0].cash==cash-catalog.find("GAPOWR")->cost,"plant cost differs");
    // Every live starting unit blocks the footprint, even without proximity.
    for(const auto& unit:world.entities) if(unit.lifetime==w::Lifetime::Placed &&
        unit.kind==w::Kind::Unit && unit.cell) {
        const p::CellXY occupied{std::int16_t(unit.cell->x),std::int16_t(unit.cell->y)};
        require(p::legal_placement(world,catalog,"GAPOWR",occupied)==false,
                "preview accepted a unit under the power footprint");
        auto blocked=world;auto blocked_factories=factories;
        require(proto::place(blocked,blocked_factories,catalog,player,"GAPOWR",occupied,frame).status!=p::Status::Done,
                "placement accepted a unit under the power footprint");
        require(blocked.entities.size()==world.entities.size() &&
                blocked_factories.factories.size()==factories.factories.size(),
                "rejected placement consumed the completed product");
    }
    place_building(world,factories,catalog,player,"GAPOWR",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"power update");
    require(*world.houses[0].power_output>0,"plant produced no power");
    build(world,factories,catalog,player,"GAPILE",frame,false);
    place_building(world,factories,catalog,player,"GAPILE",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"barracks power update");
    // A completed soldier must be abandoned when the last barracks is lost.
    {
        auto lost=world;
        auto lost_factories=factories;
        const auto started=proto::begin_production(lost,lost_factories,catalog,player,"E1",frame);
        done(started,"queue infantry before barracks loss");
        require(started.entity.has_value(),"queued infantry is missing");
        auto* pending=factory(lost_factories,*started.entity);
        require(pending,"queued infantry factory is missing");
        const auto cash=*lost.houses[0].cash;
        *lost.houses[0].cash-=catalog.find("E1")->cost;
        pending->progress=54; pending->balance=0; pending->suspended=true;
        const auto barracks=std::find_if(lost.entities.begin(),lost.entities.end(),[&](const auto& item){
            return item.house_id==player && item.type=="GAPILE" && item.lifetime==w::Lifetime::Placed;
        });
        require(barracks!=lost.entities.end(),"barracks is missing");
        const auto barracks_id=barracks->id;
        destroy_actor(lost,barracks_id);
        done(proto::tick(lost,lost_factories,catalog,frame+1),"abandon after barracks loss");
        require(actor(lost,*started.entity).lifetime==w::Lifetime::Destroyed &&
                !factory(lost_factories,*started.entity) && !lost_factories.buildings.count(barracks_id) &&
                *lost.houses[0].cash==cash &&
                count_at(lost.houses[0].infantry_owned_5440,catalog.find("E1")->heap_index)==0 &&
                count_at(lost.houses[0].buildings_active_5460,catalog.find("GAPILE")->heap_index)==0 &&
                lost_factories.house_factory_counts.at(player)[1]==0,
                "barracks loss kept a product, money, or stale counters");
        require_prototype(lost);
    }
    // A remaining barracks keeps the same queued product after one is lost.
    {
        auto surviving=world;
        auto surviving_factories=factories;
        auto local_frame=frame;
        const auto extra=build(surviving,surviving_factories,catalog,player,"GAPILE",local_frame,false);
        place_building(surviving,surviving_factories,catalog,player,"GAPILE",local_frame,base);
        done(proto::tick(surviving,surviving_factories,catalog,++local_frame),"second barracks power update");
        const auto started=proto::begin_production(surviving,surviving_factories,catalog,player,"E1",local_frame);
        done(started,"queue infantry with two barracks");
        require(started.entity.has_value(),"second barracks product is missing");
        const auto cash=*surviving.houses[0].cash;
        destroy_actor(surviving,extra);
        done(proto::tick(surviving,surviving_factories,catalog,local_frame),"continue after one barracks loss");
        auto* pending=factory(surviving_factories,*started.entity);
        require(pending && actor(surviving,*started.entity).lifetime==w::Lifetime::Created &&
                *surviving.houses[0].cash==cash && surviving_factories.house_factory_counts.at(player)[1]==1,
                "surviving barracks did not retain the queued infantry");
        pending->progress=53;
        local_frame=std::max(local_frame+1,pending->timer_start+std::uint64_t(pending->timer_duration));
        done(proto::tick(surviving,surviving_factories,catalog,local_frame),"finish at surviving barracks");
        require(!factory(surviving_factories,*started.entity) &&
                actor(surviving,*started.entity).lifetime==w::Lifetime::Placed,
                "queued infantry did not leave the surviving barracks");
        require_prototype(surviving);
    }
    const auto infantry=build(world,factories,catalog,player,"E1",frame,false,true);
    require(!factory(factories,infantry),"completed infantry stayed in its factory");
    require(actor(world,infantry).kind==w::Kind::Infantry && actor(world,infantry).active==true &&
            actor(world,infantry).in_limbo==false && actor(world,infantry).cell,
            "infantry did not leave the barracks");
    const auto foot=*actor(world,infantry).cell;
    require((*world.cells.at(std::uint32_t(foot.y)*512+foot.x).ground_occupancy_124&0x20)!=0,
            "infantry exit did not reserve its Cell");

    const auto refinery_start=frame;
    const auto pending_refinery=build(world,factories,catalog,player,"GAREFN",frame,true);
    const auto first_refinery_frames=frame-refinery_start;
    {
        auto probe=world;
        auto probe_factories=factories;
        const auto origin=place_building(probe,probe_factories,catalog,player,"GAREFN",frame,base);
        auto sealed=world;
        auto sealed_factories=factories;
        for (auto& item:sealed.cells) if (item.second.ground_occupancy_124)
            *item.second.ground_occupancy_124|=0x20;
        for (const auto& offset:p::original_foundation_cells(catalog.find("GAREFN")->placement.foundation)) {
            const auto key=std::uint32_t(origin.y+offset.y)*512+origin.x+offset.x;
            sealed.cells.at(key).ground_occupancy_124=0;
        }
        const auto before=sealed.random;
        const auto blocked=proto::place(sealed,sealed_factories,catalog,player,"GAREFN",origin,frame);
        require(blocked.status==p::Status::Rejected && actor(sealed,pending_refinery).lifetime==w::Lifetime::Created &&
                factory(sealed_factories,pending_refinery) && sealed.random.table==before.table &&
                sealed.random.first==before.first && sealed.random.second==before.second,
                "blocked refinery exit changed the world or random state");
    }
    const auto before_free_unit=world.random;
    const auto cash_before_free_unit=*world.houses[0].cash;
    place_building(world,factories,catalog,player,"GAREFN",frame,base);
    const auto* allied_refinery=catalog.find("GAREFN");
    require(allied_refinery && !allied_refinery->free_unit.empty(),"refinery has no installed free Unit");
    const auto allied_miner=find_unit(player,allied_refinery->free_unit);
    require(actor(world,allied_miner).lifetime==w::Lifetime::Placed && actor(world,allied_miner).active==true &&
            actor(world,allied_miner).maximum_hp>0 && actor(world,allied_miner).next,
            "Allied refinery did not place its free miner");
    sim::Random free_unit_random(before_free_unit);
    free_unit_random.next();
    require(world.random.table==free_unit_random.state().table && world.random.first==free_unit_random.first() &&
            world.random.second==free_unit_random.second() && *world.houses[0].cash==cash_before_free_unit,
            "free miner changed cash or used a different random stream");
    done(proto::tick(world,factories,catalog,++frame),"refinery power update");
    // A second refinery takes the same frames as the first when power is
    // enough: build time depends on cost, power, and factory count only.
    {
        auto second=world; auto second_factories=factories; auto local=frame;
        build(second,second_factories,catalog,player,"GAREFN",local,true);
        const auto second_refinery_frames=local-frame;
        // 006C22A0: 2000 * BuildSpeed 0.7 * 0.9 = 1260 frames; 1260/54 = 23 frames a stage.
        require(first_refinery_frames==54*23 && second_refinery_frames==first_refinery_frames,
                "refinery build frames differ: first "+std::to_string(first_refinery_frames)+
                ", second "+std::to_string(second_refinery_frames));
    }
    build(world,factories,catalog,player,"GAWEAP",frame,false);
    place_building(world,factories,catalog,player,"GAWEAP",frame,base);
    done(proto::tick(world,factories,catalog,++frame),"factory power update");
    const auto tank=build(world,factories,catalog,player,"MTNK",frame,false);
    require(!factory(factories,tank) && actor(world,tank).kind==w::Kind::Unit &&
            actor(world,tank).lifetime==w::Lifetime::Placed && actor(world,tank).resolved_hp>0 &&
            actor(world,tank).active==true && actor(world,tank).next,
            "tank did not leave the war factory");
    // Low power slows production that is already running. Recalc_Power
    // 004F2CC0 calls 004B9EB0, which gives each factory a new stage rate.
    // The running stage keeps its timer; the next stage uses the new rate.
    {
        auto dark=world; auto dark_factories=factories; auto local=frame;
        const auto started=proto::begin_production(dark,dark_factories,catalog,player,"MTNK",local);
        done(started,"begin tank before power loss");
        require(started.entity && started.value,"tank production result is missing");
        auto* pending=factory(dark_factories,*started.entity);
        require(pending,"tank factory is missing");
        const auto full_rate=pending->interval;
        require(full_rate==sim::factory_stage_frames(*started.value) && pending->timer_duration==full_rate,
                "tank stage rate differs from its build time");
        std::vector<std::uint32_t> plants;
        for (const auto& item:dark.entities)
            if (item.house_id==player && item.type=="GAPOWR" && item.lifetime==w::Lifetime::Placed) plants.push_back(item.id);
        require(!plants.empty(),"power plant is missing");
        for (const auto id:plants) destroy_actor(dark,id);
        done(proto::tick(dark,dark_factories,catalog,++local),"power loss update");
        const auto& house=dark.houses[0];
        require(house.power_output==0 && house.power_drain && *house.power_drain>0,"power loss did not reach the House");
        sim::BuildTimeInputs low;
        low.cost=catalog.find("MTNK")->cost; low.build_speed=catalog.build_speed;
        low.power_output=0; low.power_drain=*house.power_drain;
        low.minimum_low_power=catalog.minimum_low_power; low.maximum_low_power=catalog.maximum_low_power;
        low.low_power_penalty=catalog.low_power_penalty; low.multiple_factory=catalog.multiple_factory;
        low.factories=dark_factories.house_factory_counts.at(player)[2];
        const auto low_rate=sim::factory_stage_frames(sim::original_build_time(low));
        pending=factory(dark_factories,*started.entity);
        require(pending && pending->interval==low_rate && low_rate>full_rate && pending->timer_duration==full_rate &&
                pending->progress==0,"power loss did not slow the next tank stage");
        local=pending->timer_start+std::uint64_t(pending->timer_duration);
        done(proto::tick(dark,dark_factories,catalog,local),"first slow tank stage");
        pending=factory(dark_factories,*started.entity);
        require(pending && pending->progress==1 && pending->timer_duration==low_rate,
                "next tank stage did not use the low-power rate");
        require_prototype(dark);
    }
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
    const auto* soviet_refinery=catalog.find("NAREFN");
    require(soviet_refinery && !soviet_refinery->free_unit.empty(),"AI refinery has no installed free Unit");
    const auto soviet_miner=find_unit(enemy,soviet_refinery->free_unit);
    require(actor(world,soviet_miner).lifetime==w::Lifetime::Placed && actor(world,soviet_miner).active==true &&
            actor(world,soviet_miner).maximum_hp>0 && actor(world,soviet_miner).next,
            "AI refinery did not place its free miner");
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
