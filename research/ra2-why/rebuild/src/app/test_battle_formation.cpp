#include "battle_formation.h"
#include "battle_home.h"
#include <cassert>
#include <iostream>
using namespace ra2;
int main() {
    session::world::Snapshot world;
    rules::IniFile rules;rules.parse("[YARD]\nConstructionYard=yes\n");
    session::world::Entity unit;unit.id=1;unit.house_id=1;unit.kind=session::world::Kind::Unit;
    unit.lifetime=session::world::Lifetime::Placed;unit.resolved_hp=100;
    unit.cell=ra2map::Cell{10,10};unit.position=sim::Coord{2560,2560,0};
    auto building=unit;building.id=2;building.kind=session::world::Kind::Structure;
    auto yard=building;yard.id=3;yard.type="YARD";
    world.entities={unit,building,yard};
    assert(app::home_building(world,1,rules)->id==3);
    world.entities[2].house_id=2;
    assert(app::home_building(world,1,rules)->id==2);
    world.entities[2].house_id=1;world.entities[2].resolved_hp=0;
    assert(app::home_building(world,1,rules)->id==2);
    world.entities.resize(1);
    assert(app::home_building(world,1,rules)==nullptr);

    for(unsigned y=9;y<=11;++y) for(unsigned x=9;x<=11;++x) {
        auto& c=world.cells[y*512+x];
        c.allocated=true;c.cell_flag_100=false;c.unit_land_speed=1;
        c.heads=weapons::CellContentHeads{};c.ground_occupancy_124=0;
    }
    std::set<std::uint32_t> destinations;
    std::map<std::uint32_t,ra2map::Cell> pending;
    auto first=app::reserve_formation_cell(world,{10,10},1,pending,destinations);
    assert(first.x==10 && first.y==10);
    pending[1]=first;
    destinations.clear(); // Next command frame, before the first unit arrives.
    auto second=app::reserve_formation_cell(world,{10,10},2,pending,destinations);
    assert(second.x!=10 || second.y!=10);
    destinations.clear();
    auto own=app::reserve_formation_cell(world,{10,10},1,pending,destinations);
    assert(own.x==10 && own.y==10);
    pending.clear();destinations.clear();
    world.cells[10*512+10].cell_flag_100=true;
    auto blocked=app::reserve_formation_cell(world,{10,10},1,pending,destinations);
    assert(blocked.x!=10 || blocked.y!=10);
    std::cout<<"Formation reservations: later frames, own orders, blocked cells passed\n";
}
