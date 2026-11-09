#include "prototype_movement.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <utility>
#include <vector>
#include <iostream>
#include <limits>
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
// An open field with a tight 2x4 block of tanks on its west side.
sw::Snapshot field(int width,int height) {
    sw::Snapshot world;world.setup_progress=sw::SetupProgress::PrototypeComplete;world.random=Random(24).state();world.cell_order=std::vector<std::uint32_t>{};
    sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
    world.creation_order.push_back({sw::CreatedKind::House,1});
    for (int y=1;y<=height;++y) for (int x=1;x<=width;++x) {
        auto& c=world.cells[key(x,y)];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x,y));
    }
    return world;
}
void place(sw::Snapshot& world,std::uint32_t id,int x,int y) {
    sw::Entity unit;unit.id=id;unit.house_id=1;unit.type="GTNK";unit.kind=sw::Kind::Unit;unit.lifetime=sw::Lifetime::Placed;
    unit.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};unit.position=ground_cell_center(x,y);
    unit.layer=sw::Layer::Ground;unit.next=0;unit.resolved_hp=100;unit.hull_facing=64;
    world.entities.push_back(unit);world.creation_order.push_back({sw::CreatedKind::Entity,unit.id});
    auto& c=world.cells.at(key(x,y));c.heads->ground_head=unit.id;c.ground_occupancy_124=32;
}
bool same_cell(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
int chebyshev(ra2map::Cell a,ra2map::Cell b) { return std::max(std::abs(int(a.x)-int(b.x)),std::abs(int(a.y)-int(b.y))); }
// Drive a group order like the Battle loop: a blocked Unit is canceled and
// its order is issued again every 15 frames. Return the frames to arrive,
// and the largest step back away from each goal.
struct GroupRun { unsigned frames=0;int worst_retreat=0;bool all_arrived=false;unsigned blocked=0,idle=0,steps=0,direct=0;std::int64_t closest=std::numeric_limits<std::int64_t>::max(); };
GroupRun drive_group(sw::Snapshot& world,PrototypeMovement& movement,const std::vector<std::pair<std::uint32_t,ra2map::Cell>>& orders,int speed,unsigned limit) {
    std::map<std::uint32_t,ra2map::Cell> pending;std::map<std::uint32_t,int> best;GroupRun run;
    const auto cell_of=[&](std::uint32_t id) { for (const auto& e:world.entities) if (e.id==id) return *e.cell; throw std::runtime_error("lost Unit"); };
    for (const auto& [id,goal]:orders) {
        if (movement.issue(world,id,goal,speed).status==PrototypeMoveStatus::Complete) pending[id]=goal;
        else throw std::runtime_error("group order was rejected");
        best[id]=chebyshev(cell_of(id),goal);run.direct+=unsigned(best[id]);
    }
    std::map<std::uint32_t,ra2map::Cell> last;for (const auto& [id,goal]:orders) last[id]=cell_of(id);
    for (std::uint32_t frame=1;frame<=limit && !pending.empty();++frame) {
        const auto motion=movement.tick(world,frame);
        require(motion.status==PrototypeMoveStatus::Complete,"group frame failed");
        run.blocked+=unsigned(motion.blocked.size());
        for (const auto id:motion.blocked) movement.cancel(world,id);
        for (const auto id:motion.arrived) pending.erase(id);
        for (const auto& [id,goal]:pending) {
            if (!movement.moving(id)) ++run.idle;
            if (frame%15==0 && !movement.moving(id)) movement.issue(world,id,goal,speed);
            const int now=chebyshev(cell_of(id),goal);
            run.worst_retreat=std::max(run.worst_retreat,now-best[id]);best[id]=std::min(best[id],now);
        }
        for (auto& [id,cell]:last) { const auto now=cell_of(id); if (!same_cell(now,cell)) ++run.steps; cell=now; }
        // The closest two Unit centers come, in world units.
        for (std::size_t i=0;i<world.entities.size();++i) for (std::size_t j=i+1;j<world.entities.size();++j) {
            const auto dx=std::int64_t(world.entities[i].position->x)-world.entities[j].position->x;
            const auto dy=std::int64_t(world.entities[i].position->y)-world.entities[j].position->y;
            std::int64_t d=0;while ((d+1)*(d+1)<=dx*dx+dy*dy) ++d;
            run.closest=std::min(run.closest,d);
        }
        run.frames=frame;
    }
    run.all_arrived=pending.empty();
    return run;
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
    {
        // QA round 1: eight Grizzlies in a tight block took 50 s to move a
        // short way, and some drove back. Each tank gets its own goal
        // cell around the click, like Battle formation_cell().
        auto world=field(40,20);PrototypeMovement movement;
        std::vector<std::pair<std::uint32_t,ra2map::Cell>> orders;
        const ra2map::Cell goals[]={{30,10},{30,9},{31,9},{31,10},{31,11},{30,11},{29,11},{29,10}};
        for (std::uint32_t i=0;i<8;++i) {
            place(world,201+i,3+int(i%2),7+int(i/2));
            orders.push_back({201+i,goals[i]});
        }
        // Grizzly Speed=7 gives original_rule_speed(7)=17 world units a frame.
        const auto run=drive_group(world,movement,orders,17,900);
        std::cout<<"group of 8 arrived="<<run.all_arrived<<" frames="<<run.frames<<" worst_retreat="<<run.worst_retreat<<" blocked="<<run.blocked<<" idle_unit_frames="<<run.idle<<" steps="<<run.steps<<" direct="<<run.direct<<" closest="<<run.closest<<'\n';
        // The far tank drives 28 Cells: 28*256/17 is about 422 frames alone.
        // Before the fix: 544 frames, one tank stopped on a wrong Cell, and two
        // tanks came 94 units apart. Corner waits cost a few frames.
        require(run.all_arrived && run.frames<=500 && run.closest>=181,"group of 8 was slow to reach its goals");
        require(run.worst_retreat<=1,"a group Unit drove back away from its goal");
        for (std::uint32_t i=0;i<8;++i) require(same_cell(*world.entities[i].cell,goals[i]),"group Unit stopped away from its own goal");
    }
    {
        // Two tanks drive head-on along one row. Each waits or steps
        // aside; neither gives up or drives back.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,301,5,6);place(world,302,20,6);
        const auto run=drive_group(world,movement,{{301,{20,6}},{302,{5,6}}},17,600);
        std::cout<<"head-on arrived="<<run.all_arrived<<" frames="<<run.frames<<" worst_retreat="<<run.worst_retreat<<" blocked="<<run.blocked<<'\n';
        require(run.all_arrived && run.blocked==0 && run.worst_retreat<=1 && run.frames<=270,"head-on tanks did not pass each other");
        require(same_cell(*world.entities[0].cell,{20,6}) && same_cell(*world.entities[1].cell,{5,6}),"head-on tanks stopped away from their goals");
    }
    {
        // Two tanks side by side drive diagonally across each other. They
        // must not pass through the same Cell corner at the same time.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,601,10,4);place(world,602,11,4);
        const auto run=drive_group(world,movement,{{601,{13,7}},{602,{8,7}}},17,400);
        std::cout<<"crossing arrived="<<run.all_arrived<<" frames="<<run.frames<<" closest="<<run.closest<<'\n';
        // 181 is a Cell center to its corner: a pass beside a stopped tank.
        require(run.all_arrived && run.closest>=181,"crossing tanks overlapped at a Cell corner");
    }
    {
        // A stopped tank holds the goal. The mover stops on the near side
        // of the goal and does not drive around it.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,401,20,6);place(world,402,5,6);
        const auto run=drive_group(world,movement,{{402,{20,6}}},17,400);
        require(run.all_arrived && same_cell(*world.entities[1].cell,{19,6}),"mover did not stop on the near side of a held goal");
        // A second order to the same held goal does not move it again.
        const auto before=*world.entities[1].position;
        require(movement.issue(world,402,{20,6},17).status==PrototypeMoveStatus::Complete,"repeat order to a held goal failed");
        const auto again=movement.tick(world,1000);
        require(again.arrived.size()==1 && world.entities[1].position->x==before.x && world.entities[1].position->y==before.y,
                "repeat order to a held goal moved a Unit that was already beside it");
    }
    {
        // Speed: 10 straight Cells at 17 units a frame is 2560/17, so 151
        // frames. The old code stopped at each Cell center: 160 frames.
        auto world=field(30,12);PrototypeMovement movement;place(world,501,5,6);
        const auto run=drive_group(world,movement,{{501,{15,6}}},17,400);
        std::cout<<"single tank 10 Cells frames="<<run.frames<<'\n';
        require(run.all_arrived && run.frames==151,"a tank lost speed at Cell centers");
    }
    std::cout<<"prototype routed 3 Units around obstacles in "<<frames<<" shared frames; replay and atomic rejection passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
