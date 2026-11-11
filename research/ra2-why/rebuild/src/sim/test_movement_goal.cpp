#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include "prototype_movement.h"
#include "ground.h"
#include "world.h"
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
bool same_cell(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
void place(sw::Snapshot& world,std::uint32_t id,int x,int y) {
    sw::Entity unit;unit.id=id;unit.house_id=1;unit.type="GTNK";unit.kind=sw::Kind::Unit;unit.lifetime=sw::Lifetime::Placed;
    unit.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    unit.position=ground_cell_center(static_cast<std::int16_t>(x),static_cast<std::int16_t>(y));
    unit.resolved_hp=100;unit.layer=sw::Layer::Ground;unit.next=0;unit.hull_facing=64;
    world.entities.push_back(unit);world.creation_order.push_back({sw::CreatedKind::Entity,id});
    auto& c=world.cells.at(std::uint32_t(unit.cell->y)*512+std::uint32_t(unit.cell->x));
    c.heads->ground_head=id;c.ground_occupancy_124=32;
}
}
int main() try {
    sw::Snapshot world;
    world.setup_progress=sw::SetupProgress::PrototypeComplete;
    world.random=Random(24).state();
    world.cell_order=std::vector<std::uint32_t>{};
    sw::House house;house.id=1;house.name="P";house.country="C";
    world.houses.push_back(house);
    world.creation_order.push_back({sw::CreatedKind::House,1});
    for (int y=1;y<=12;++y) for (int x=1;x<=14;++x) {
        auto& c=world.cells[std::uint32_t(y)*512+std::uint32_t(x)];
        c.allocated=true;c.cell_flag_100=false;c.heads=weapons::CellContentHeads{};
        c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});c.terrain->floor_world_height=1;c.ground_occupancy_124=0;
        world.cell_order->push_back(static_cast<std::uint32_t>(y)*512+static_cast<std::uint32_t>(x));
    }
    place(world,1,2,2);

    PrototypeMovement movement;
    // Unknown ID has no goal.
    require(!movement.goal(999),"unknown ID must have no goal");
    // Issued route exposes its goal.
    const ra2map::Cell goal{12,12};
    require(movement.issue(world,1,goal,64).status==PrototypeMoveStatus::Complete,"issue failed");
    auto got=movement.goal(1);
    require(got && same_cell(*got,goal),"issued route must expose its goal");
    // A replacement order changes the goal.
    const ra2map::Cell later{13,11};
    require(movement.issue(world,1,later,64).status==PrototypeMoveStatus::Complete,"replace failed");
    got=movement.goal(1);
    require(got && same_cell(*got,later),"replacement must expose the new goal");
    // A rejected replacement keeps the old goal. A Cell outside the map is refused.
    require(movement.issue(world,1,{600,600},64).status==PrototypeMoveStatus::Invalid,"off-map goal was accepted");
    got=movement.goal(1);
    require(got && same_cell(*got,later),"rejected replacement must keep the old goal");
    // Cancel clears the goal.
    movement.cancel(world,1);
    require(!movement.goal(1),"canceled route must have no goal");
    // Arrival clears the goal.
    const ra2map::Cell near{3,2};
    require(movement.issue(world,1,near,64).status==PrototypeMoveStatus::Complete,"arrival issue failed");
    for (std::uint32_t frame=1;frame<=100 && movement.moving(1);++frame)
        require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"arrival tick failed");
    require(!movement.moving(1),"unit did not arrive");
    require(!movement.goal(1),"arrived route must have no goal");
    std::cout<<"test_movement_goal: 6 checks passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr<<"test_movement_goal: "<<e.what()<<'\n';
    return 1;
}
