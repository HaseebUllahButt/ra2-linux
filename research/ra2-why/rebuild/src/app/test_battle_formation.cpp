#include "battle_formation.h"
#include <cassert>
#include <iostream>
using namespace ra2;
int main() {
    session::world::Snapshot world;
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
