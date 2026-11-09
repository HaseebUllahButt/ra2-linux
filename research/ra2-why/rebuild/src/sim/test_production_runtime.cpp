#include "production_runtime.h"
#include "../session/world/world.h"
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace ra2::sim::production;
namespace {
template<class T> T get() { long long v; if (!(std::cin>>v)) throw std::runtime_error("truncated case"); return static_cast<T>(v); }
std::vector<std::int32_t> list() {
    const auto n=get<unsigned>(); if (n>64) throw std::runtime_error("list bound");
    std::vector<std::int32_t> out(n); for (auto& v:out) v=get<std::int32_t>(); return out;
}
std::map<std::int32_t,std::int32_t> counts() {
    std::map<std::int32_t,std::int32_t> out; for (int i=0;i<8;++i) out[i]=get<std::int32_t>(); return out;
}
int code(CanBuild r) {
    switch (r) { case CanBuild::Cannot: return 0; case CanBuild::Can: return 1; case CanBuild::LimitReached: return -1;
    default: throw std::runtime_error("unresolved comparison case"); }
}
void can_build_cases() {
    int rtti;
    while (std::cin>>rtti) {
        CanBuildType t; t.rtti=static_cast<Rtti>(rtti); t.heap_index=get<int>(); t.constructor_flag_aa4=get<int>();
        t.prerequisite=list(); t.prerequisite_override=list(); t.tech_level=get<int>();
        t.requires_stolen_allied=get<int>(); t.requires_stolen_soviet=get<int>();
        t.required_houses=get<std::uint32_t>(); t.forbidden_houses=get<std::uint32_t>();
        t.super_weapon=get<int>(); t.build_limit=get<int>(); t.infantry_flag_c14=get<int>();
        CanBuildHouse h; h.tech_level=get<int>(); h.stolen_allied=get<int>(); h.stolen_soviet=get<int>();
        h.country=get<int>(); std::array<std::uint32_t,4> masks; for (auto& m:masks) m=get<std::uint32_t>(); h.stolen_masks=masks;
        h.super_weapon_disabled[0]=get<int>(); h.super_weapon_disabled[1]=get<int>();
        h.flag_134=get<int>(); h.flag_135=get<int>(); h.active_buildings=counts();
        h.owned_buildings=counts(); h.owned_units=counts(); h.owned_infantry=counts(); h.owned_aircraft=counts();
        h.built_buildings=counts(); h.built_units=counts(); h.built_infantry=counts(); h.built_aircraft=counts();
        std::vector<HouseBuilding> buildings(get<unsigned>());
        for (auto& b:buildings) {
            b.limbo=get<int>(); b.flag_530=get<int>(); b.mission=get<int>(); b.mission_a0=get<int>();
            for (auto& u:b.upgrades) { const auto v=get<int>(); if (v>=0) u=v; }
        }
        h.buildings=buildings; h.infantry_c14_matches=get<int>();
        std::vector<std::optional<std::pair<Rtti,std::int32_t>>> factories(get<unsigned>());
        for (auto& f:factories) { const auto present=get<int>(); const auto r=get<int>(); const auto i=get<int>();
            if (present) f=std::make_pair(static_cast<Rtti>(r),i); }
        h.factory_objects=factories;
        CanBuildRules g; g.super_weapons_allowed=get<int>(); g.session_type=get<int>();
        std::array<std::vector<std::int32_t>,6> groups; for (auto& grp:groups) grp=list(); g.generic_prerequisites=groups;
        for (int i=0;i<8;++i) g.powers_up_building[i]=get<int>();
        const bool limit_only=get<int>(), queued=get<int>();
        std::cout<<code(original_can_build(t,h,g,limit_only,queued))<<'\n';
    }
}
PlacementCell empty_cell() {
    PlacementCell c; c.ground=std::vector<CellOccupant>{}; c.occupancy_124=0; c.height=0; c.ramp=0;
    c.overlay=-1; c.flags_140=0; c.land_type=0; return c;
}
void placement_cases() {
    int width;
    while (std::cin>>width) {
        PlacementMap m; m.width=width; m.local_x=get<int>(); m.local_y=get<int>(); m.local_width=get<int>(); m.local_height=get<int>();
        CellXY origin{get<std::int16_t>(),get<std::int16_t>()};
        PlacementBuilding b; b.foundation=get<int>(); b.place_anywhere=get<int>();
        std::array<bool,12> buildable; for (auto& v:buildable) v=get<int>(); m.buildable=buildable;
        m.scenario_init=0; m.object_scan=true; m.editor_mode=false; m.fallback=empty_cell();
        const auto n=get<unsigned>();
        for (unsigned i=0;i<n;++i) {
            CellXY xy{get<std::int16_t>(),get<std::int16_t>()};
            PlacementCell c; std::vector<CellOccupant> occ(get<unsigned>());
            for (auto& o:occ) { o.rtti=static_cast<Rtti>(get<int>()); o.techno=get<int>()&1; }
            c.ground=occ; c.occupancy_124=get<int>(); c.height=get<int>(); c.ramp=get<int>(); c.overlay=get<int>();
            c.flags_140=get<std::uint32_t>(); c.land_type=get<int>();
            m.cells[packed_cell(xy)]=c;
        }
        const auto r=original_legal_placement(m,origin,b);
        if (r!=Placement::Legal && r!=Placement::Illegal) throw std::runtime_error("unresolved placement case");
        std::cout<<(r==Placement::Legal)<<'\n';
    }
}
unsigned checks=0;
void require(bool ok,const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }
void self_test() {
    // 004B9630 preserves paid stages; 004B97C0 refunds only paid money.
    {
        namespace w=ra2::session::world;
        w::Snapshot world; world.setup_progress=w::SetupProgress::Complete;
        world.geometry=ra2::session::PlacementGeometry{100,100,{2,2,96,90}};
        w::House owner; owner.id=1; owner.cash=800;
        owner.infantry_owned_5440=std::map<std::int32_t,std::int32_t>{{0,1}};
        world.houses.push_back(owner);
        w::Entity product; product.id=2; product.house_id=1; product.type="E1";
        product.kind=w::Kind::Infantry; product.lifetime=w::Lifetime::Created;
        world.entities.push_back(product);
        Catalog catalog; TypeFacts type; type.id="E1"; type.rtti=Rtti::InfantryType; type.cost=200;
        catalog.infantry.push_back(type);
        FactorySet factories; FactoryState factory; factory.house_id=1; factory.slot=1;
        factory.object=2; factory.object_type="E1"; factory.progress=20; factory.balance=125;
        factory.interval=5; factory.timer_duration=5; factory.suspended=false;
        factories.factories.push_back(factory);
        require(hold_production(world,factories,catalog,1,"E1",12).status==Status::Done,"hold running product");
        const auto& held=factories.factories.front();
        require(held.progress==20 && held.balance==125 && held.suspended && held.interval==0 &&
            held.timer_duration==0 && held.timer_start==12,"hold preserves progress and clears timer");
        require(*world.houses.front().cash==800,"hold has no refund");
        require(tick(world,factories,catalog,1000).status==Status::Done &&
            factories.factories.front().progress==20 && *world.houses.front().cash==800,
            "held factory never advances or drains money");
        require(hold_production(world,factories,catalog,1,"E1",13).status==Status::Rejected,"hold twice rejected");
        require(cancel_production(world,factories,catalog,1,"E1",14).status==Status::Done,"cancel held product");
        require(*world.houses.front().cash==875,"refund equals cost minus balance");
        require(world.houses.front().infantry_owned_5440->at(0)==0,"cancel drops owned count");
        require(world.entities.front().lifetime==w::Lifetime::Destroyed,"cancel destroys product");
        require(!factories.factories.front().object && factories.factories.front().progress==0,"cancel resets factory");
        require(cancel_production(world,factories,catalog,1,"E1",15).status==Status::Rejected,"cancel twice rejected");
        require(*world.houses.front().cash==875,"cancel twice gives no extra refund");
        world.entities.front().lifetime=w::Lifetime::Created;
        (*world.houses.front().infantry_owned_5440)[0]=1;
        auto& complete=factories.factories.front();
        complete.object=2; complete.object_type="E1"; complete.progress=54; complete.balance=0;
        require(cancel_production(world,factories,catalog,1,"E1",16).status==Status::Done &&
            *world.houses.front().cash==1075,"completed product refunds full cost before exit");
    }
    // Foundation lists from 00454EF0: GACNST uses 4x4 (index 18).
    require(original_foundation_cells(18).size()==16,"4x4 foundation");
    require(original_foundation_cells(9).size()==8,"special 3x3 foundation");
    require(original_foundation_cells(21).empty(),"custom foundation is empty");
    // Deploy helpers 0045EC50/0045EC90.
    require(!original_deploy_without_offset(false,false,false,false),"normal deploy uses NW offset");
    require(original_deploy_facing(false,false,false,false)==0x80,"normal deploy facing");
    require(original_deploy_facing(true,false,false,false)==0x40,"sensor deploy facing");
    require(original_deploy_facing(false,false,false,true)==0,"artillery deploy facing");
    // Unknown inputs never publish a decision.
    CanBuildType t; t.tech_level=1; CanBuildHouse h; CanBuildRules g;
    require(original_can_build(t,h,g,false,false)==CanBuild::NeedsInput,"unknown house rejects");
    PlacementMap m; PlacementBuilding b; b.foundation=0;
    require(original_legal_placement(m,{10,10},b)==Placement::NeedsInput,"unknown map rejects");
    std::cout<<"Production runtime self-test: "<<checks<<" checks passed\n";
}
}
int main(int argc,char** argv) try {
    if (argc==2 && std::string_view(argv[1])=="--can-build-cases") can_build_cases();
    else if (argc==2 && std::string_view(argv[1])=="--placement-cases") placement_cases();
    else if (argc==1) self_test();
    else throw std::runtime_error("usage: sim_production_runtime_test [--can-build-cases|--placement-cases]");
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
