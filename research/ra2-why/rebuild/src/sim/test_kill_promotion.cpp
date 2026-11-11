#include "kill_promotion.h"
#include "damage_runtime.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <sstream>
using namespace ra2;
namespace world=ra2::session::world;
void check(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
world::Snapshot make_world() {
    world::Snapshot w;
    w.random = sim::Random(0).state();
    for (int y = 46; y <= 54; ++y) {
        for (int x = 46; x <= 56; ++x) {
            world::CellState c;
            c.allocated = true;
            c.heads.emplace();
            c.cell_flag_100 = false;
            c.ground_occupancy_124 = 0;
            c.terrain.emplace();
            c.terrain->cell = {static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)};
            c.terrain->floor_world_height = 0;
            c.terrain->ramp_index = 0;
            c.terrain->overlay_type_index = -1;
            w.cells.emplace(y * 512 + x, c);
        }
    }
    world::House a;
    a.id = 101;
    a.reload_multiplier = 1;
    a.firepower_multiplier = 1;
    world::House b = a;
    b.id = 102;
    b.human_controlled = true;
    w.houses = {a, b};
    world::Entity source;
    source.id = 1;
    source.house_id = 101;
    source.type = "SRC";
    source.kind = world::Kind::Infantry;
    source.lifetime = world::Lifetime::Placed;
    source.position = sim::Coord{50 * 256 + 128, 50 * 256 + 128, 0};
    source.cell = ra2map::Cell{50, 50};
    source.next = 0;
    source.resolved_hp = source.maximum_hp = 200;
    source.active = true;
    source.in_limbo = false;
    source.hull_facing_raw = 0;
    source.turret_facing_raw = 0;
    source.weapon_timer_start_frame = -1;
    source.weapon_timer_duration = 0;
    source.burst_index = 0;
    source.armor_multiplier = 1;
    source.house_armor_multiplier = 1;
    source.firepower_multiplier = 1;
    source.collision_eligible = true;
    source.collision_virtual_position = source.position;
    source.veterancy = 1.0;
    source.mission = 13;
    auto target = source;
    target.id = 2;
    target.house_id = 102;
    target.type = "TGT";
    target.position = sim::Coord{54 * 256 + 128, 50 * 256 + 128, 0};
    target.cell = ra2map::Cell{54, 50};
    target.resolved_hp = target.maximum_hp = 1000;
    target.veterancy = 0.0;
    target.collision_virtual_position = target.position;
    w.entities = {source, target};
    return w;
}

rules::IniFile load(std::string extra={}) {
    rules::IniFile r;std::string text(
        "[General]\nVeteranRatio=3\nVeteranCap=2\n"
        "[CombatDamage]\nMaxDamage=10000\n"
        "[WEAPON]\nDamage=2000\nROF=60\nRange=10\nWarhead=WH\nProjectile=PROJ\n"
        "[WH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
        "[SRC]\nPrimary=WEAPON\nArmor=heavy\nCost=200\n"
        "[TGT]\nStrength=1000\nArmor=heavy\nCost=100\n"
        "[A]\nCostInfantryMult=4\nCostUnitsMult=4\n"
        "[B]\nCostInfantryMult=0.5\nCostUnitsMult=1.5\n");
    rules::IniFile additions;additions.parse(extra);
    for(const auto& section:additions.sections()) {
        std::string body;for(const auto& entry:section.entries) body+=entry.key+"="+entry.value+"\n";
        const auto header="["+section.name+"]\n";const auto at=text.find(header);
        if(at==std::string::npos)text+=header+body;else text.insert(at+header.size(),body);
    }
    r.parse(text);return r;
}
world::Snapshot known() {
    auto w=make_world();w.entities[0].veterancy=0;
    w.entities[1].has_trigger=false;
    w.houses[0].country="A";w.houses[1].country="B";
    w.houses[0].original_index=0;w.houses[1].original_index=1;
    w.houses[0].alliance_mask_5674=0;
    w.cells.at(50*512+50).heads->ground_head=1;
    w.cells.at(50*512+54).heads->ground_head=2;
    return w;
}
int main(int argc,char** argv) {
    if(argc>1 && std::string(argv[1])=="--arithmetic") {
        int cost,killer,victim;double factor,value,rank,ratio,cap;
        while(std::cin>>cost>>factor>>value>>killer>>victim>>rank>>ratio>>cap)
            std::cout<<sim::base_promotion_price(cost,static_cast<float>(factor))<<' '
                <<std::setprecision(17)<<sim::base_kill_gain(value,killer,victim,rank,ratio,cap)<<'\n';
        return 0;
    }
    const auto rules=load();
    for(auto sk:{world::Kind::Infantry,world::Kind::Unit})
    for(auto tk:{world::Kind::Infantry,world::Kind::Unit})
    for(double rank:{0.,1.,2.}) {
        auto w=known();w.entities[0].kind=sk;w.entities[1].kind=tk;w.entities[1].veterancy=rank;
        const auto plan=sim::base_prepare_kill_promotion(w,rules,1,2);
        const int kp=sk==world::Kind::Infantry?100:300,vp=tk==world::Kind::Infantry?50:150;
        check(plan.value && *plan.value==sim::base_kill_gain(0,kp,vp,rank,3,2),"rank/price resolution");
    }
    for(int mode=0;mode<10;++mode) {
        auto w=known();auto r=rules;
        if(mode==0)w.houses[0].original_index.reset();
        if(mode==1)w.houses[0].alliance_mask_5674.reset();
        if(mode==2)w.houses[1].country.clear();
        if(mode==3)w.entities[0].veterancy.reset();
        if(mode==4)w.entities[1].has_trigger.reset();
        if(mode==5)r=load("[SRC]\nMissileSpawn=yes\n");
        if(mode==6)r=load("[TGT]\nPassengers=1\n");
        if(mode==7)r=load("[TGT]\nCyborg=yes\n");
        if(mode==8)w.entities[1].has_trigger=true;
        if(mode==9)r=load("[B]\nCostInfantryMult=0\n");
        const auto before=w.entities[0].veterancy;
        const auto p=sim::base_prepare_kill_promotion(w,r,1,2);
        if(p.value || p.missing.empty() || w.entities[0].veterancy!=before)
            throw std::runtime_error("missing state changed rank: "+std::to_string(mode));
    }
    {
        auto w=known();w.houses[1].alliance_mask_5674=1;
        check(sim::base_prepare_kill_promotion(w,rules,1,2).value.has_value(),"reverse alliance blocked hostile kill");
        w.houses[0].alliance_mask_5674=2;
        check(!sim::base_prepare_kill_promotion(w,rules,1,2).value,"direct alliance awarded credit");
        w.entities[1].house_id=101;
        check(!sim::base_prepare_kill_promotion(w,rules,1,2).value,"same house awarded credit");
        w=known();check(!sim::base_prepare_kill_promotion(w,load("[SRC]\nTrainable=no\n"),1,2).value,"untrainable gained rank");
        w.entities[0].lifetime=world::Lifetime::Destroyed;w.entities[0].resolved_hp=0;
        check(sim::base_prepare_kill_promotion(w,rules,1,2).value.has_value(),"dead killer lost valid credit");
        check(!sim::base_prepare_kill_promotion(w,rules,0,2).value,"null killer gained credit");
    }
    const std::string exe=argc>1?argv[1]:"";
    for(auto kind:{world::Kind::Infantry,world::Kind::Unit})
    for(int mode=0;mode<4;++mode) {
        auto w=known();w.entities[1].kind=kind;
        if(mode==1) {w.houses[0].original_index.reset();w.houses[1].country.clear();}
        if(mode==2)w.entities[1].next.reset();
        if(mode==3)w.entities[1].has_trigger.reset();
        sim::PrototypeCombatRuntime runtime(rules,{}, {},exe);
        check(runtime.attack(w,1,2),"attack refused");
        std::size_t deaths=0;bool missing=false;
        for(unsigned frame=0;frame<8;++frame) {
            const auto tick=runtime.tick(w,frame);deaths+=tick.destroyed;
            missing|=!tick.unresolved.empty();
        }
        if(mode==2) {
            check(deaths==0 && *w.entities[0].veterancy==0 && *w.entities[1].resolved_hp==1000,"failed removal awarded credit or health");
        } else {
            check(deaths==1 && *w.entities[1].resolved_hp==0 && w.entities[1].lifetime==world::Lifetime::Destroyed,"death did not complete once");
            check(*w.entities[0].veterancy==(mode==0?sim::base_kill_gain(0,100,kind==world::Kind::Infantry?50:150,0,3,2):0),"runtime credit");
        }
        check(missing==(mode==2),"promotion changed damage error reporting");
    }
    std::cout<<"kill promotion: focused checks passed\n";
}
