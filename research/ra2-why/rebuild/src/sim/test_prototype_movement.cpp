#include "prototype_movement.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x,int y) { return std::uint32_t(y)*512+std::uint32_t(x); }
sw::Snapshot fixture() {
    sw::Snapshot world;world.setup_progress=sw::SetupProgress::PrototypeComplete;world.random=Random(24).state();world.cell_order=std::vector<std::uint32_t>{};
    sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
    world.creation_order.push_back({sw::CreatedKind::House,1});
    for (int y=1;y<=12;++y) for (int x=1;x<=14;++x) {
        auto& c=world.cells[key(x,y)];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x,y));
    }
    // TODO-ORIGINAL: prototype obstacles use explicit test occupancy, not art.
    for (int y=2;y<=8;++y) if (y!=5) world.cells.at(key(6,y)).ground_occupancy_124=1;
    const int ys[]={2,9,10};
    for (unsigned i=0;i<3;++i) {
        sw::Entity unit;unit.id=101+i;unit.house_id=1;unit.type=i==2?"E1":"HTNK";
        unit.kind=i==2?sw::Kind::Infantry:sw::Kind::Unit;unit.lifetime=sw::Lifetime::Placed;
        unit.cell=ra2map::Cell{2,static_cast<std::uint16_t>(ys[i])};unit.position=ground_cell_center(2,ys[i]);
        unit.layer=sw::Layer::Ground;unit.next=0;unit.resolved_hp=100;unit.hull_facing=64;
        world.entities.push_back(unit);world.creation_order.push_back({sw::CreatedKind::Entity,unit.id});
        auto& c=world.cells.at(key(2,ys[i]));c.heads->ground_head=unit.id;c.ground_occupancy_124=32;
    }
    return world;
}
}
int main() try {
    auto idle_world=fixture();PrototypeMovement idle;
    auto idle_incomplete=idle_world;idle_incomplete.setup_progress=sw::SetupProgress::PackedMapRead;
    require(idle.tick(idle_incomplete,10).status==PrototypeMoveStatus::NeedsInput,"idle frame accepted unfinished setup");
    const auto cell_address=reinterpret_cast<std::uintptr_t>(&idle_world.cells.at(key(2,2)));
    const auto entity_address=reinterpret_cast<std::uintptr_t>(idle_world.entities.data());
    const auto random=idle_world.random;
    const auto idle_result=idle.tick(idle_world,10);
    require(idle_result.status==PrototypeMoveStatus::Complete && idle_result.moved.empty() &&
            idle_result.arrived.empty() && idle_result.blocked.empty() &&
            reinterpret_cast<std::uintptr_t>(&idle_world.cells.at(key(2,2)))==cell_address &&
            reinterpret_cast<std::uintptr_t>(idle_world.entities.data())==entity_address &&
            idle_world.random.table==random.table && idle_world.random.first==random.first && idle_world.random.second==random.second,
            "idle frame changed or copied the shared world");
    require(idle.tick(idle_world,10).status==PrototypeMoveStatus::Invalid &&
            idle.tick(idle_world,9).status==PrototypeMoveStatus::Invalid &&
            idle.tick(idle_world,11).status==PrototypeMoveStatus::Complete,"idle frame did not retain the shared clock");
    require(idle.issue(idle_world,101,{4,2},64).status==PrototypeMoveStatus::Complete &&
            idle.tick(idle_world,11).status==PrototypeMoveStatus::Invalid &&
            idle.tick(idle_world,12).status==PrototypeMoveStatus::Complete && idle_world.entities[0].locomotor_moving==true,
            "movement did not resume on the next shared frame");
    auto original=fixture();auto a=original,b=original;PrototypeMovement first,replay;
    auto incomplete=original;incomplete.setup_progress=sw::SetupProgress::PackedMapRead;
    require(first.issue(incomplete,101,{4,2},64).status==PrototypeMoveStatus::NeedsInput,
            "unfinished prototype setup was accepted");
    const ra2map::Cell goals[]={{10,2},{10,9},{10,10}};
    for (unsigned i=0;i<3;++i) {
        require(first.issue(a,101+i,goals[i],64).status==PrototypeMoveStatus::Complete,"prototype command rejected");
        require(replay.issue(b,101+i,goals[i],64).status==PrototypeMoveStatus::Complete,"prototype replay command rejected");
    }
    auto missing=a;missing.cells.at(key(3,9)).terrain->floor_world_height.reset();
    const auto old=missing.entities[0].position;
    const auto rejected=first.tick(missing,0);
    require(rejected.status==PrototypeMoveStatus::NeedsInput && missing.entities[0].position->x==old->x &&
            missing.entities[0].position->y==old->y && missing.random.table==a.random.table,
            "prototype partial frame was published");
    unsigned frames=0;bool saw_concurrent=false;
    while (first.active_count() && frames<300) {
        const auto one=first.tick(a,frames),two=replay.tick(b,frames);
        if (one.status!=PrototypeMoveStatus::Complete) std::cerr<<one.detail<<'\n';
        require(one.status==PrototypeMoveStatus::Complete && two.status==PrototypeMoveStatus::Complete,"prototype frame failed");
        saw_concurrent|=one.moved.size()>=2;
        for (unsigned i=0;i<3;++i) {
            require(a.entities[i].position->x==b.entities[i].position->x && a.entities[i].position->y==b.entities[i].position->y &&
                    a.entities[i].cell->x==b.entities[i].cell->x && a.entities[i].cell->y==b.entities[i].cell->y &&
                    a.entities[i].next==b.entities[i].next,"prototype Unit replay differs");
        }
        require(a.random.table==original.random.table && a.random.first==original.random.first && a.random.second==original.random.second,
                "prototype movement changed RNG state");
        ++frames;
    }
    require(saw_concurrent && frames<300 && !replay.active_count(),"prototype Units did not move together");
    for (unsigned i=0;i<3;++i) {
        const auto& unit=a.entities[i];const auto& cell=a.cells.at(key(goals[i].x,goals[i].y));
        require(unit.cell->x==goals[i].x && unit.cell->y==goals[i].y && cell.heads->ground_head==unit.id &&
                *cell.ground_occupancy_124==32 && unit.next==0 && unit.locomotor_moving==false &&
                unit.collision_virtual_position && unit.collision_virtual_position->x==unit.position->x &&
                unit.collision_virtual_position->y==unit.position->y,"prototype Unit or Cell did not reach its goal");
    }
    const auto repeated=first.tick(a,frames-1);
    require(repeated.status==PrototypeMoveStatus::Invalid,"duplicate shared frame was accepted");
    auto canceled=original;PrototypeMovement cancel_test;
    require(cancel_test.issue(canceled,101,{4,2},64).status==PrototypeMoveStatus::Complete &&
            cancel_test.tick(canceled,0).status==PrototypeMoveStatus::Complete && canceled.entities[0].locomotor_moving==true,
            "prototype moving flag did not start");
    cancel_test.cancel(canceled,101);
    require(!cancel_test.moving(101) && canceled.entities[0].locomotor_moving==false,"prototype cancel did not stop activity");
    auto obstacle=original;PrototypeMovement blocked;
    require(blocked.issue(obstacle,101,{6,2},64).status==PrototypeMoveStatus::Complete,"prototype did not choose an open nearby Cell");
    require(blocked.issue(obstacle,101,{10,2},0).status==PrototypeMoveStatus::Invalid,"unknown speed was invented");
    auto group=original;PrototypeMovement group_move;
    require(group_move.issue(group,102,{10,9},128).status==PrototypeMoveStatus::Complete &&
            group_move.issue(group,103,{10,9},128).status==PrototypeMoveStatus::Complete,"prototype group order failed");
    unsigned group_frame=0;
    while (group_move.active_count() && group_frame<300) {
        require(group_move.tick(group,group_frame++).status==PrototypeMoveStatus::Complete,"prototype group frame failed");
    }
    require(!group_move.active_count() && (group.entities[1].cell->x!=group.entities[2].cell->x ||
            group.entities[1].cell->y!=group.entities[2].cell->y),"prototype group did not spread around the goal");
    std::cout<<"prototype routed 3 Units around obstacles in "<<frames<<" shared frames; replay and atomic rejection passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
