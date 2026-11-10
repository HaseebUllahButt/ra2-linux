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
namespace w=ra2::session::world;
// A flat 100x100 map, Cells 60..139. Water (LandType 2) fills x 100..119,
// y 80..99; every other Cell is Clear. A computer construction yard (4x4)
// stands at (90,90).
struct NavalFixture { Catalog catalog; w::Snapshot world; FactorySet factories; };
NavalFixture naval_fixture(bool water=true) {
    NavalFixture n; auto& c=n.catalog; auto& s=n.world; auto& f=n.factories;
    c.countries={"Russians"};
    std::array<std::array<float,8>,12> speeds{};
    for (int l=0;l<12;++l) for (int k=0;k<8;++k) speeds[std::size_t(l)][std::size_t(k)]=(k==5)?(l==2?1.0f:0.0f):1.0f;
    c.land_speed=speeds;
    std::array<bool,12> buildable{}; buildable.fill(true); buildable[2]=false; c.buildable=buildable;
    c.build_speed=0.7; c.minimum_low_power=0.5f; c.maximum_low_power=0.8f; c.low_power_penalty=1; c.multiple_factory=0.8f;
    TypeFacts yard; yard.id="YARD"; yard.rtti=Rtti::BuildingType; yard.heap_index=0; yard.can.heap_index=0;
    yard.factory=Rtti::BuildingType; yard.construction_yard=true; yard.placement.foundation=18; yard.owner=1;
    yard.can.tech_level=1; yard.cost=3000; yard.strength=1000;
    TypeFacts ship; ship.id="SHIP"; ship.rtti=Rtti::BuildingType; ship.heap_index=1; ship.can.heap_index=1;
    ship.placement.naval=true; ship.placement.speed_type=5; ship.placement.foundation=18; ship.owner=1;
    ship.can.tech_level=-1; ship.cost=1000; ship.strength=1500; ship.factory=Rtti::UnitType; ship.planning_side=1;
    c.buildings={yard,ship}; c.shipyards={1};
    s.setup_progress=w::SetupProgress::Complete;
    s.geometry=ra2::session::PlacementGeometry{100,100,{2,2,96,90}};
    for (int y=60;y<140;++y) for (int x=60;x<140;++x) {
        w::CellState cell; cell.allocated=true; cell.heads=ra2::weapons::CellContentHeads{};
        ra2::sim::TerrainCell t; t.cell={std::uint16_t(x),std::uint16_t(y)}; t.tile_index=0; t.height_level=0; t.ramp_index=0;
        t.floor_world_height=0; t.overlay_type_index=-1; t.bridge=false;
        t.land_type=water && x>=100 && x<120 && y>=80 && y<100?2:0; cell.terrain=t;
        cell.cell_flags_140=0; cell.ground_occupancy_124=0; cell.bridge_occupancy_128=0;
        s.cells[std::uint32_t(y*512+x)]=cell;
    }
    w::House h; h.id=1; h.original_index=0; h.country="Russians"; h.country_index=0; h.side_index=1;
    h.cash=5000; h.tech_level=10; h.human_controlled=false; h.house_flag_135=false;
    h.stolen_tech_204=false; h.stolen_tech_205=false; h.stolen_mask_208=0; h.stolen_mask_20c=0; h.stolen_mask_210=0; h.stolen_mask_214=0;
    h.power_output=100; h.power_drain=0;
    for (auto* m:{&h.buildings_active_5460,&h.buildings_owned_5420,&h.buildings_ever_54a0,&h.units_owned_5430,&h.units_ever_54b0,
                  &h.infantry_owned_5440,&h.infantry_ever_54c0,&h.aircraft_owned_5450,&h.aircraft_ever_54d0}) *m=std::map<std::int32_t,std::int32_t>{};
    s.houses.push_back(h); s.creation_order.push_back({w::CreatedKind::House,1});
    w::Entity e; e.id=2; e.house_id=1; e.kind=w::Kind::Structure; e.type="YARD"; e.lifetime=w::Lifetime::Placed;
    e.cell=ra2map::Cell{90,90}; e.position=ra2::sim::Coord{90*256+128,90*256+128,0}; e.layer=w::Layer::Ground;
    e.next=0; e.object_field_74=1; e.object_field_76=0; e.on_map=true; e.mission=0; e.original_type_index=0;
    s.entities.push_back(e); s.creation_order.push_back({w::CreatedKind::Entity,2});
    for (const auto o:original_foundation_cells(18)) s.cells.at(std::uint32_t((90+o.y)*512+90+o.x)).heads->ground_head=2;
    f.flags.session_type=1; f.flags.super_weapons=true; f.flags.build_off_ally=false; f.flags.player_house=1;
    f.buildings[2]=BuildingState{}; f.house_factory_counts[1]={0,0,0,0,0,0}; f.power_blackout_idle[1]=true;
    return n;
}
// A WaterBound building (SpeedType 5) on water: 004718B0 reads the Float
// column of the land speed table (00850EA0) and skips the land checks.
// The ring search 004F03C0 -> 005502E0 finds a Cell whose 6x6 block is
// water, picks it by frame, and rejects it beyond AINavalYardAdjacency.
// A computer house starts the shipyard without Can_Build (0044B480).
void naval_cases() {
    auto n=naval_fixture();
    require(legal_placement(n.world,n.catalog,"SHIP",{100,80})==true,"shipyard is legal on open water");
    require(legal_placement(n.world,n.catalog,"SHIP",{98,80})==false,"shipyard is illegal on land");
    const auto grid=placement_cells(n.world,n.catalog,"SHIP",{98,80});
    require(grid && grid->size()==16,"shipyard cursor grid has 16 Cells");
    int legal_cells=0; for (const auto& [cell,r]:*grid) { (void)cell; if (r==Placement::Legal) ++legal_cells; }
    require(legal_cells==8,"cursor marks only the water Cells legal");
    const auto first=naval_yard_site(n.world,n.factories,n.catalog,1,{90,90},0);
    require(first.status==SiteStatus::Found && first.cell==CellXY{100,80},"first ring candidate for frame 0");
    // Ring 10 admits (100,80) from the top row, then (100,81)..(100,94).
    const auto later=naval_yard_site(n.world,n.factories,n.catalog,1,{90,90},18);
    require(later.status==SiteStatus::Found && later.cell==CellXY{100,83},"frame 18 picks candidate 18 mod 15");
    auto near=n; near.catalog.naval_yard_adjacency=10;
    require(naval_yard_site(near.world,near.factories,near.catalog,1,{90,90},0).status==SiteStatus::None,
        "a Cell beyond AINavalYardAdjacency is rejected");
    auto dry=naval_fixture(false);
    require(naval_yard_site(dry.world,dry.factories,dry.catalog,1,{90,90},0).status==SiteStatus::None,
        "no water gives no Cell");
    // TechLevel -1: the sidebar refuses it, the computer path starts it.
    require(begin_production(n.world,n.factories,n.catalog,1,"SHIP",0).status==Status::Rejected,"sidebar refuses TechLevel -1");
    const auto started=begin_computer_building(n.world,n.factories,n.catalog,1,"SHIP",0);
    require(started.status==Status::Done && started.entity,"computer starts its shipyard");
    for (auto& x:n.factories.factories) if (x.object==*started.entity) x.progress=54;
    require(place(n.world,n.factories,n.catalog,1,"SHIP",first.cell,1).status==Status::Done,"shipyard placed on water");
    const w::Entity* placed=nullptr; for (const auto& e:n.world.entities) if (e.id==*started.entity) placed=&e;
    require(placed && placed->lifetime==w::Lifetime::Placed && placed->cell->x==100 && placed->cell->y==80,"shipyard stands at the site");
}
void self_test() {
    // 004B9630 preserves paid stages; 004B97C0 refunds only paid money.
    {
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
    // Queue limit: FactoryClass::Queue_Type 004B9440 caps one queue at
    // Rules +0xE8 (default 30, stored at 0072C27F). Queued items cost
    // nothing until they become the active object. RemoveOneFromQueue
    // 004B9DF0 removes the first queued entry. StartProduction 004B9D70
    // starts the front entry when the factory is idle.
    {
        w::Snapshot world; world.setup_progress=w::SetupProgress::Complete;
        world.geometry=ra2::session::PlacementGeometry{100,100,{2,2,96,90}};
        w::House owner; owner.id=1; owner.cash=5000; owner.tech_level=10;
        owner.power_output=100; owner.power_drain=10; owner.human_controlled=true;
        for (auto* m:{&owner.buildings_active_5460,&owner.buildings_owned_5420,&owner.buildings_ever_54a0,
                      &owner.units_owned_5430,&owner.units_ever_54b0,&owner.infantry_owned_5440,
                      &owner.infantry_ever_54c0,&owner.aircraft_owned_5450,&owner.aircraft_ever_54d0})
            *m=std::map<std::int32_t,std::int32_t>{};
        world.houses.push_back(owner); world.creation_order.push_back({w::CreatedKind::House,1});
        w::Entity barracks; barracks.id=2; barracks.house_id=1; barracks.kind=w::Kind::Structure;
        barracks.type="BARR"; barracks.lifetime=w::Lifetime::Placed; barracks.cell=ra2map::Cell{90,90};
        barracks.position=ra2::sim::Coord{90*256+128,90*256+128,0}; barracks.layer=w::Layer::Ground;
        barracks.next=0; barracks.object_field_74=1; barracks.object_field_76=0; barracks.on_map=true;
        barracks.mission=0; barracks.resolved_hp=400; barracks.maximum_hp=400; barracks.original_type_index=1;
        world.entities.push_back(barracks); world.creation_order.push_back({w::CreatedKind::Entity,2});
        Catalog catalog; catalog.build_speed=0.7;
        catalog.minimum_low_power=0.5f; catalog.maximum_low_power=0.8f;
        catalog.low_power_penalty=1; catalog.multiple_factory=0.8f; catalog.wall_coefficient=3;
        TypeFacts barr; barr.id="BARR"; barr.rtti=Rtti::BuildingType; barr.heap_index=1; barr.can.heap_index=1;
        barr.factory=Rtti::InfantryType; barr.cost=500; barr.strength=400;
        barr.can.tech_level=1; barr.owner=1;
        TypeFacts gi; gi.id="E1"; gi.rtti=Rtti::InfantryType; gi.heap_index=0; gi.can.heap_index=0;
        gi.cost=200; gi.strength=100; gi.can.tech_level=1; gi.can.build_limit=0x7FFFFFFF; gi.owner=1;
        TypeFacts e2; e2.id="E2"; e2.rtti=Rtti::InfantryType; e2.heap_index=3; e2.can.heap_index=3;
        e2.cost=500; e2.strength=100; e2.can.tech_level=1; e2.can.build_limit=0x7FFFFFFF; e2.owner=1;
        TypeFacts limited; limited.id="LTD"; limited.rtti=Rtti::InfantryType; limited.heap_index=2;
        limited.can.heap_index=2; limited.cost=100; limited.strength=100; limited.can.tech_level=1;
        limited.can.build_limit=-2; limited.owner=1;
        catalog.buildings={barr}; catalog.infantry={gi,e2,limited};
        FactorySet factories; factories.flags.session_type=1; factories.flags.super_weapons=true;
        factories.flags.build_off_ally=false; factories.flags.player_house=1;
        factories.buildings[2]=BuildingState{}; factories.house_factory_counts[1]={0,0,0,0,0,0};
        factories.power_blackout_idle[1]=true;
        // GI: cost 200 x 0.7 x 0.9 = 126 build frames; stage interval 126/54 = 2.
        const auto first=begin_production(world,factories,catalog,1,"E1",0);
        require(first.status==Status::Done && first.value && *first.value==125,"GI builds in 125 frames");
                require(factories.factories.front().balance==200 && factories.factories.front().progress==0 && factories.factories.front().interval==2 &&
            factories.factories.front().timer_duration==2 && !factories.factories.front().suspended,"GI factory starts at stage 0, interval 2");
        // The build limit counts the queued entries of the type. The GI
        // object occupies the infantry factory, so each LTD click queues one.
                require(begin_production(world,factories,catalog,1,"LTD",0).status==Status::Done,
            "limited type queues once");
        require(factories.factories.back().queued.size()==1,"the LTD queue holds 1");
        require(begin_production(world,factories,catalog,1,"LTD",0).status==Status::Done,
            "limited type queues twice");
        require(factories.factories.back().queued.size()==2,"the LTD queue holds 2");
        require(begin_production(world,factories,catalog,1,"LTD",0).status==Status::Rejected,
            "BuildLimit -2 refuses the third LTD");
        require(*world.houses.front().cash==5000,"queued LTDs cost nothing");
        // Right click on a queued icon removes one entry of that type.
        require(cancel_production(world,factories,catalog,1,"LTD",0).status==Status::Done,
            "right click removes one queued LTD");
        require(factories.factories.back().queued.size()==1,"the LTD queue drops to 1");
        require(*world.houses.front().cash==5000,"queued cancel refunds nothing");
        require(cancel_production(world,factories,catalog,1,"LTD",0).status==Status::Done,
            "right click removes the last queued LTD");
        require(factories.factories.back().queued.empty(),"the LTD queue is empty");
        // The whole infantry queue is one pool of 30 including the active
        // object: 28 GI clicks plus one Engineer-style type fill it to
        // 29 queued + 1 active = 30 (reference: 29 GIs queued, the Engineer
        // icon took 1, 29 + 1 = 30).
        for (int click=0;click<28;++click)
            require(begin_production(world,factories,catalog,1,"E1",0).status==Status::Done,
                "queue click accepted");
        require(begin_production(world,factories,catalog,1,"E2",0).status==Status::Done,
            "the mixed type takes the 30th slot");
        require(factories.factories.front().queued.size()==29,"the queue holds 29 queued items");
        require(factories.factories.front().queued.back()=="E2","the last entry is the mixed type");
        require(*world.houses.front().cash==5000,"queued items cost nothing");
        require(begin_production(world,factories,catalog,1,"E1",0).status==Status::Rejected,
            "click 31 is refused at the queue limit");
        require(begin_production(world,factories,catalog,1,"E2",0).status==Status::Rejected,
            "click 32 for the mixed type is refused too");
        // A same-type right click removes a queued entry and keeps the active
        // object and the cash.
        const auto active_before=factories.factories.front().object;
        require(cancel_production(world,factories,catalog,1,"E1",0).status==Status::Done,
            "same-type right click removes a queued GI");
        require(*world.houses.front().cash==5000 &&
            factories.factories.front().object==active_before &&
            factories.factories.front().queued.size()==28,
            "same-type cancel keeps the active object and the cash");
        // A tick with no pending queue advance leaves the entities untouched.
        const auto entities_before=world.entities.size();
        require(tick(world,factories,catalog,1).status==Status::Done,"tick with a full queue");
        require(world.entities.size()==entities_before,"no pending queue means no entity copy");
        // Run the GI factory to completion, then the queue advances.
        std::uint64_t frame=0;
        for (frame=1;factories.factories.front().progress!=54 && frame<=2000;++frame)
            require(tick(world,factories,catalog,frame).status==Status::Done,"tick");
        require(factories.factories.front().progress==54 && factories.factories.front().suspended && *world.houses.front().cash==4800,
            "GI finished after 126 frames and paid 200");
        // Deliver the finished GI out of the factory (Exit_Object); the
        // factory is idle and StartProduction 004B9D70 runs on the next tick.
        { auto& done=factories.factories.front(); done.object=0; done.object_type.clear();
          done.suspended=true; done.progress=0; done.interval=0; done.timer_duration=0; }
        require(tick(world,factories,catalog,++frame).status==Status::Done,"advance tick");
        require(factories.factories.front().object && factories.factories.front().object_type=="E1" && factories.factories.front().progress==0 &&
            factories.factories.front().balance==200 && factories.factories.front().queued.size()==27 &&
            world.houses.front().infantry_owned_5440->at(0)==2,
            "the idle factory started the next queued GI");
        // A right click on the building icon removes a queued GI first.
        require(cancel_production(world,factories,catalog,1,"E1",0).status==Status::Done,
            "right click removes a queued GI");
        require(*world.houses.front().cash==4800 &&
            factories.factories.front().queued.size()==26,
            "queued GI cancel refunds nothing");
        require(cancel_production(world,factories,catalog,1,"E1",0).status==Status::Done,
            "right click removes the next queued GI");
        require(factories.factories.front().queued.size()==25 &&
            *world.houses.front().cash==4800,"queued GI cancel refunds nothing");
    }
    naval_cases();
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
