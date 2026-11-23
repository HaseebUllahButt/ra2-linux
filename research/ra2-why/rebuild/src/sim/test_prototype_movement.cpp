#include "prototype_movement.h"
#include "../app/battle_shroud.h"
#include <algorithm>
#include <cstdint>
#include <cmath>
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

rules::IniFile test_rules() {
    rules::IniFile ini;
    ini.parse(
        "[General]\nCloseEnough=2.25\nBlockagePathDelay=60\nTrackedUphill=1.0\nTrackedDownhill=1.2\n"
        "[AudioVisual]\nConditionYellow=50%\n"
        "[Clear]\nFoot=100%\nTrack=100%\nWheel=100%\n"
        "[Tiberium]\nFoot=90%\nTrack=70%\nWheel=50%\n"
        "[Beach]\nFoot=0%\nTrack=0%\nWheel=0%\n"
        "[MTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=5\nAccelerates=false\nCrusher=yes\nStrength=300\n"
        "[ACCEL]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nStrength=300\n"
        "[SLOW]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=1\nAccelerates=false\n"
        "[E1]\nCrushable=yes\n[SHK]\nCrushable=no\n");
    return ini;
}
void put(sw::Snapshot& world,std::uint32_t id,const char* type,sw::Kind kind,std::uint32_t house,int x,int y,int facing=64) {
    place(world,id,x,y);auto& e=world.entities.back();e.type=type;e.kind=kind;e.house_id=house;e.hull_facing=facing;
    if (kind==sw::Kind::Unit) e.maximum_hp=300,e.resolved_hp=300;
}
// Frames until a Unit reaches goal, or zero.
unsigned run_to(sw::Snapshot& world,PrototypeMovement& movement,std::uint32_t id,ra2map::Cell goal,unsigned limit,PrototypeMoveResult* all=nullptr) {
    require(movement.issue(world,id,goal,17).status==PrototypeMoveStatus::Complete,"order rejected");
    for (std::uint32_t frame=1;frame<=limit;++frame) {
        const auto r=movement.tick(world,frame);
        require(r.status==PrototypeMoveStatus::Complete,"frame failed");
        if (all) all->crushed.insert(all->crushed.end(),r.crushed.begin(),r.crushed.end());
        for (const auto a:r.arrived) if (a==id) return frame;
    }
    return 0;
}
const sw::Entity& unit(const sw::Snapshot& w,std::uint32_t id) {
    for (const auto& e:w.entities) if (e.id==id) return e;
    throw std::runtime_error("missing Unit");
}
// A soldier on spot 2..4 of a shared Cell, linked as the new chain head.
void soldier(sw::Snapshot& world,std::uint32_t id,std::uint32_t house,int x,int y,int spot) {
    static constexpr int sx[5]={128,64,192,64,192},sy[5]={128,64,64,192,192};
    sw::Entity e;e.id=id;e.house_id=house;e.type="E1";e.kind=sw::Kind::Infantry;e.lifetime=sw::Lifetime::Placed;
    e.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    e.position=sim::Coord{x*256+sx[spot],y*256+sy[spot],0};e.layer=sw::Layer::Ground;e.resolved_hp=100;e.hull_facing=64;
    auto& c=world.cells.at(key(x,y));e.next=c.heads->ground_head;c.heads->ground_head=id;*c.ground_occupancy_124|=1u<<spot;
    world.entities.push_back(e);world.creation_order.push_back({sw::CreatedKind::Entity,id});
}
void original_rules_tests() {
    const auto rules=test_rules();
    {
        // 004A2670: Ground[Tiberium].Track is 70%, so speed 17 becomes
        // int(17*0.7)=11 a frame. The original drive takes 149 frames for
        // 10 Cells at speed 11 (97 on clear ground).
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        for (int x=6;x<=15;++x) world.cells.at(key(x,6)).terrain->land_type=5;
        put(world,901,"MTNK",sw::Kind::Unit,1,5,6);
        const auto frames=run_to(world,movement,901,{15,6},400);
        std::cout<<"ore 10 Cells frames="<<frames<<'\n';
        require(frames==149,"ore did not slow a tracked tank like the original");
    }
    {
        // 005D4540 at or below ConditionYellow (50%): speed times 0.75, 12.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,902,"MTNK",sw::Kind::Unit,1,5,6);world.entities.back().resolved_hp=150;
        const auto frames=run_to(world,movement,902,{15,6},400);
        std::cout<<"damaged 10 Cells frames="<<frames<<'\n';
        require(frames==137,"a damaged tank did not slow like the original");
    }
    {
        // 004A2670 waits until the hull faces the first step exactly, and
        // 004B8E90 turns ROT*256 of 65536 a frame. A ROT 5 tank facing east
        // turns 180 degrees in 26 frames (the oracle: 1.2 to 1.7 s).
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,903,"MTNK",sw::Kind::Unit,1,15,6,64);
        require(movement.issue(world,903,{5,6},17).status==PrototypeMoveStatus::Complete,"reverse order failed");
        unsigned turned=0;
        for (std::uint32_t frame=1;frame<60;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"turn frame failed");
            if (unit(world,903).position->x!=15*256+128) { turned=frame-1;break; }
        }
        std::cout<<"reverse turn frames="<<turned<<'\n';
        require(turned==26 && *unit(world,903).hull_facing_raw==0xC000,"tank did not turn in place at its ROT");
        // Without rules a tank turns at once (old behaviour) - the profile matters.
        auto plain=field(30,12);PrototypeMovement instant;place(plain,904,15,6);
        require(instant.issue(plain,904,{5,6},17).status==PrototypeMoveStatus::Complete &&
                instant.tick(plain,1).status==PrototypeMoveStatus::Complete && plain.entities[0].position->x<15*256+128,
                "a tank without rules waited to turn");
    }
    {
        // 004A1000 with Accelerates (default yes): the speed fraction rises
        // by AccelerationFactor 0.03 a frame from zero, and falls by
        // Speed*DeaccelerationFactor inside SlowdownDistance to at least 0.3.
        auto world=field(40,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,905,"ACCEL",sw::Kind::Unit,1,5,6);
        require(movement.issue(world,905,{30,6},17).status==PrototypeMoveStatus::Complete,"acceleration order failed");
        require(movement.tick(world,1).status==PrototypeMoveStatus::Complete,"acceleration frame failed");
        require(std::abs(movement.motion(905)->speed-0.03)<1e-9,"acceleration did not start at 0.03");
        for (std::uint32_t frame=2;frame<=40;++frame) movement.tick(world,frame);
        require(movement.motion(905)->speed==1.0,"acceleration did not reach full speed");
        double low=1.0;unsigned frames=0;
        for (std::uint32_t frame=41;frame<=600 && movement.moving(905);++frame) {
            movement.tick(world,frame);frames=frame;
            if (movement.motion(905)->stepping) low=std::min(low,movement.motion(905)->speed);
        }
        std::cout<<"accelerating 25 Cells frames="<<frames<<" lowest speed="<<low<<'\n';
        require(!movement.moving(905) && std::abs(low-0.30000001192092896)<1e-9,"deceleration did not stop at 0.3");
    }
    {
        // 00705160: a Crusher kills enemy crushable Infantry in the Cell it
        // enters. A friendly or a Crushable=no soldier still blocks.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,906,"MTNK",sw::Kind::Unit,1,5,6);
        put(world,907,"E1",sw::Kind::Infantry,2,8,6);
        PrototypeMoveResult all;
        const auto frames=run_to(world,movement,906,{10,6},200,&all);
        require(frames && all.crushed.size()==1 && all.crushed[0]==907 &&
                unit(world,907).lifetime==sw::Lifetime::Destroyed && same_cell(*unit(world,906).cell,{10,6}) &&
                world.cells.at(key(8,6)).heads->ground_head==0,"tank did not crush enemy infantry on its path");
        auto spared=field(30,12);PrototypeMovement other;other.use_rules(rules);
        put(spared,908,"MTNK",sw::Kind::Unit,1,5,6);
        put(spared,909,"E1",sw::Kind::Infantry,1,8,6);
        put(spared,910,"SHK",sw::Kind::Infantry,2,8,5);put(spared,911,"SHK",sw::Kind::Infantry,2,8,7);
        PrototypeMoveResult none;
        run_to(spared,other,908,{10,6},300,&none);
        require(none.crushed.empty() && unit(spared,909).lifetime==sw::Lifetime::Placed &&
                unit(spared,910).lifetime==sw::Lifetime::Placed,"tank crushed a friend or a Crushable=no soldier");
    }
    {
        // The original start puts up to three soldiers of one House in a
        // Cell (spot bits 4, 8, 16). Each one can take an order; the old
        // code refused every soldier that was not alone with bit 0x20.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        soldier(world,920,1,5,6,2);soldier(world,921,1,5,6,3);soldier(world,922,1,5,6,4);
        const ra2map::Cell goals[]={{9,4},{9,6},{9,8}};
        for (unsigned i=0;i<3;++i)
            require(movement.issue(world,920+i,goals[i],10).status==PrototypeMoveStatus::Complete,"a soldier in a shared Cell was refused");
        for (std::uint32_t frame=1;frame<300 && movement.active_count();++frame)
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"shared soldier frame failed");
        require(!movement.active_count() && *world.cells.at(key(5,6)).ground_occupancy_124==0 &&
                world.cells.at(key(5,6)).heads->ground_head==0,"shared soldiers did not leave their Cell");
        for (unsigned i=0;i<3;++i) require(same_cell(*unit(world,920+i).cell,goals[i]) && unit(world,920+i).next==0u,"a soldier missed its goal");
        // Soldiers of one House share a goal Cell on free spots.
        PrototypeMovement again;again.use_rules(rules);
        require(again.issue(world,921,{9,4},10).status==PrototypeMoveStatus::Complete,"soldier refused a shared goal");
        for (std::uint32_t frame=400;frame<700 && again.active_count();++frame) again.tick(world,frame);
        require(same_cell(*unit(world,921).cell,{9,4}) && *world.cells.at(key(9,4)).ground_occupancy_124==(4u|8u),
                "two soldiers did not share a goal Cell");
    }
    {
        // A tank in a corridor meets friendly soldiers. They step aside and
        // the tank passes; the old code saw their Cell as a wall.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        for (int x=4;x<=12;++x) world.cells.at(key(x,5)).ground_occupancy_124=1,world.cells.at(key(x,7)).ground_occupancy_124=1;
        world.cells.at(key(8,5)).ground_occupancy_124=0; // A side pocket the tank cannot use.
        put(world,923,"MTNK",sw::Kind::Unit,1,5,6);
        soldier(world,924,1,8,6,2);
        const auto frames=run_to(world,movement,923,{11,6},400);
        std::cout<<"tank past friendly soldier frames="<<frames<<'\n';
        require(frames && unit(world,924).lifetime==sw::Lifetime::Placed && same_cell(*unit(world,924).cell,{8,5}),
                "tank did not pass a friendly soldier that stepped aside");
    }
    {
        // A match has two movement owners: one for the miners and one for
        // all other Units. A miner (here a tank) in a lane meets a friendly
        // soldier that walks west for the other owner. The old code also
        // pushed the soldier north-east. The two pushes held the soldier
        // in place and the miner waited without end (MP02T2, HARV at 73,114).
        auto world=field(30,12);PrototypeMovement miners,others;miners.use_rules(rules);others.use_rules(rules);
        for (int y=3;y<=9;++y) {
            if (y!=6) world.cells.at(key(7,y)).ground_occupancy_124=1;
            if (y!=5) world.cells.at(key(9,y)).ground_occupancy_124=1;
        }
        put(world,925,"MTNK",sw::Kind::Unit,1,8,7,0);
        soldier(world,926,1,8,6,2);
        require(others.issue(world,926,{7,6},10).status==PrototypeMoveStatus::Complete &&
                miners.issue(world,925,{8,2},17).status==PrototypeMoveStatus::Complete,"two owner orders failed");
        unsigned frames=0;
        for (std::uint32_t frame=1;frame<=400 && !frames;++frame) {
            const auto first=miners.tick(world,frame);
            require(first.status==PrototypeMoveStatus::Complete && others.tick(world,frame).status==PrototypeMoveStatus::Complete,"two owner frame failed");
            for (const auto a:first.arrived) if (a==925) frames=frame;
        }
        std::cout<<"tank past a soldier of the other owner frames="<<frames<<'\n';
        require(frames && same_cell(*unit(world,925).cell,{8,2}),"a tank waited without end for a soldier that the other owner moves");
        require(same_cell(*unit(world,926).cell,{7,6}) && !miners.moving(926) && !others.moving(926),
                "the soldier did not reach the goal of its own owner");
    }
    {
        // Two movement owners can leave a Unit with Cell links that this
        // owner cannot move. That must stop only the one Unit. The old code returned an error for the whole tick, each
        // frame, so no Unit of that owner moved again (MP02T2, Brutal
        // computer: the miner stood still from frame 22471).
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,931,"MTNK",sw::Kind::Unit,1,5,3);
        put(world,932,"MTNK",sw::Kind::Unit,1,5,8);
        require(movement.issue(world,931,{6,3},17).status==PrototypeMoveStatus::Complete &&
                movement.issue(world,932,{10,8},17).status==PrototypeMoveStatus::Complete,"refused transfer orders failed");
        // After the order, a soldier of another owner is linked behind the tank in its Cell.
        soldier(world,933,1,5,3,2);
        world.cells.at(key(5,3)).heads->ground_head=931;
        for (auto& e:world.entities) { if (e.id==931) e.next=933; if (e.id==933) e.next=0; }
        unsigned frames=0;bool refused=false;
        for (std::uint32_t frame=1;frame<=400 && !frames;++frame) {
            const auto r=movement.tick(world,frame);
            require(r.status==PrototypeMoveStatus::Complete,"one Unit with bad links failed the whole frame");
            for (const auto b:r.blocked) if (b==931) refused=true;
            for (const auto a:r.arrived) if (a==932) frames=frame;
        }
        std::cout<<"tank beside a Unit with bad links frames="<<frames<<'\n';
        require(frames && same_cell(*unit(world,932).cell,{10,8}),"one tank with bad links stopped another tank");
        require(refused && same_cell(*unit(world,931).cell,{5,3}),"the tank with bad links did not stay and report a block");
        require(!movement.moving(931),"the refused tank kept a route it cannot follow");
    }
    {
        // The wait for soldiers to step aside ends after BlockagePathDelay
        // (60 frames). Here another owner keeps the soldier busy in the
        // near gap of a wall. The tank waits, then takes the far gap; it
        // does not push the soldier.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        for (int y=2;y<=12;++y) if (y!=6) world.cells.at(key(8,y)).ground_occupancy_124=1;
        put(world,927,"MTNK",sw::Kind::Unit,1,5,6);
        soldier(world,928,1,8,6,2);
        require(movement.issue(world,927,{14,6},17).status==PrototypeMoveStatus::Complete,"busy soldier order failed");
        unsigned frames=0,waited=0;
        for (std::uint32_t frame=1;frame<=600 && !frames;++frame) {
            for (auto& e:world.entities) if (e.id==928) e.locomotor_moving=true;
            const auto r=movement.tick(world,frame);
            require(r.status==PrototypeMoveStatus::Complete,"busy soldier frame failed");
            if (same_cell(*unit(world,927).cell,{7,6}) && r.moved.empty()) ++waited;
            for (const auto a:r.arrived) if (a==927) frames=frame;
        }
        std::cout<<"tank around a busy soldier frames="<<frames<<" waited="<<waited<<'\n';
        require(frames && waited>=60 && same_cell(*unit(world,928).cell,{8,6}) && !movement.moving(928),
                "a tank did not plan around a busy soldier after BlockagePathDelay");
    }
    {
        // Ground cost zero (Beach for Track) cannot be entered.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        for (int y=1;y<=12;++y) if (y!=2) world.cells.at(key(10,y)).terrain->land_type=6;
        put(world,912,"MTNK",sw::Kind::Unit,1,5,6);
        require(run_to(world,movement,912,{15,6},400),"tank found no way around the beach");
        // It crossed at row 2 only.
        require(same_cell(*unit(world,912).cell,{15,6}),"tank did not reach its goal past the beach");
    }
    {
        // Two tanks drive into the same Cell from both sides. The first
        // claims both Cells of its step; the second waits and then stops
        // short (CloseEnough). They never overlap: the old code let both
        // start and left one on the shared Cell edge.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,913,5,6);place(world,914,7,6);
        const auto run=drive_group(world,movement,{{913,{6,6}},{914,{6,6}}},17,200);
        std::cout<<"same goal closest="<<run.closest<<'\n';
        require(run.all_arrived && run.closest>=256,"two tanks met on one Cell edge");
    }
    {
        // BlockagePathDelay (60 frames): a tank waits behind a moving Unit
        // that has not stalled before it plans around. The blocker turns
        // slowly in place (ROT 1: 128 frames for half a turn) in a corridor.
        auto world=field(30,12);PrototypeMovement movement;movement.use_rules(rules);
        for (int x=8;x<=12;++x) world.cells.at(key(x,5)).ground_occupancy_124=1,world.cells.at(key(x,7)).ground_occupancy_124=1;
        put(world,915,"SLOW",sw::Kind::Unit,1,8,6,192);
        put(world,916,"SLOW",sw::Kind::Unit,1,7,6,64);
        require(movement.issue(world,915,{12,6},17).status==PrototypeMoveStatus::Complete &&
                movement.issue(world,916,{13,6},17).status==PrototypeMoveStatus::Complete,"blockage orders failed");
        unsigned left=0;
        for (std::uint32_t frame=1;frame<200;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"blockage frame failed");
            if (unit(world,916).position->x!=7*256+128 || unit(world,916).position->y!=6*256+128 ||
                unit(world,916).hull_facing.value_or(0)!=64) { left=frame;break; }
        }
        std::cout<<"blocked tank left after frames="<<left<<'\n';
        require(left>60,"a tank planned around a moving blocker before BlockagePathDelay");
    }
}
}
int main() try {
    {
        auto world=fixture();
        rules::IniFile sight;sight.parse("[HTNK]\nSight=1\n[E1]\nSight=1\n");
        app::BattleShroud shroud;shroud.update(world,sight);
        const ra2map::Cell goal{10,2};
        require(!shroud.revealed(1,goal),"move test goal must start in black shroud");
        PrototypeMovement movement;
        require(movement.issue(world,101,goal,64).status==PrototypeMoveStatus::Complete,
                "black ground refused a move order");
        for(unsigned frame=0;frame<300 && movement.moving(101);++frame)
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,
                    "move through black ground failed");
        require(same_cell(*world.entities[0].cell,goal),"unit did not reach black ground");
        shroud.update(world,sight);
        require(shroud.revealed(1,goal),"unit did not reveal its destination");
    }

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
        // Infantry rests on spot 2 (bit 4, offset 192,64; table 00850E50).
        const std::uint32_t mark=i==2?4u:32u;
        require(unit.cell->x==goals[i].x && unit.cell->y==goals[i].y && cell.heads->ground_head==unit.id &&
                *cell.ground_occupancy_124==mark && (i!=2 || (unit.position->x%256==192 && unit.position->y%256==64)) && unit.next==0 && unit.locomotor_moving==false &&
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
    // The original drive finishes its current track after a stop: the tank
    // ends on a Cell center, then stops.
    require(!cancel_test.moving(101),"prototype cancel kept the order");
    for (std::uint32_t frame=1;frame<40;++frame)
        require(cancel_test.tick(canceled,frame).status==PrototypeMoveStatus::Complete,"canceled step failed");
    require(canceled.entities[0].locomotor_moving==false && canceled.entities[0].position->x%256==128 &&
            canceled.entities[0].position->y%256==128,"prototype cancel did not stop on a Cell center");
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
        // 004A2670: a blocked tank less than CloseEnough (2.25 Cells) from
        // its goal ends its move there.
        for (std::uint32_t i=0;i<8;++i) {
            const auto c=*world.entities[i].cell;
            const auto dx=(int(c.x)-int(goals[i].x))*256,dy=(int(c.y)-int(goals[i].y))*256;
            require(dx*dx+dy*dy<576*576,"group Unit stopped away from its own goal");
        }
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
        // A tank stopped off its Cell center (an order canceled mid-step).
        // Its straight line to the next Cell crosses a Cell held by a
        // stopped tank. It must first go back to its own center, not
        // report "blocked" on every new order.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,701,5,5);place(world,702,5,6);
        world.entities[0].position->x=5*256+250;
        const auto run=drive_group(world,movement,{{701,{4,6}}},17,300);
        std::cout<<"off-center arrived="<<run.all_arrived<<" blocked="<<run.blocked<<'\n';
        require(run.all_arrived && run.blocked==0 && same_cell(*world.entities[0].cell,{4,6}),"off-center tank was blocked by a corner Cell");
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
        // The mover has entered the Cell beside a held goal, but has
        // not reached its center. A repeated order must center it.
        auto world=field(30,12);PrototypeMovement movement;
        place(world,801,20,6);place(world,802,19,6);
        world.entities[1].position->x=19*256+250;
        require(movement.issue(world,802,{20,6},17).status==PrototypeMoveStatus::Complete,"near-goal order failed");
        for(unsigned frame=0;frame<30 && movement.active_count();++frame)
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"near-goal tick failed");
        require(!movement.active_count() && world.entities[1].position->x==19*256+128,
                "mover stopped off-center beside a held goal");
    }
    {
        // Speed: the original drive spends 7 speed units on each track
        // sample: 24 samples a straight Cell, 32 a diagonal one (004A1000,
        // 004B0F20). DriveSegment over the original game.exe tables gives
        // these frame counts for 10 Cells. The old code moved 17 world
        // units a frame and took 151 frames (straight, speed 17).
        struct Case { int speed;bool diagonal;unsigned frames; };
        for (const auto& c:{Case{17,false,97},Case{15,false,110},Case{17,true,134},Case{15,true,152}}) {
            auto world=field(30,20);PrototypeMovement movement;place(world,501,5,6);
            if (c.diagonal) world.entities[0].hull_facing=96;
            const ra2map::Cell goal{15,static_cast<std::uint16_t>(c.diagonal?16:6)};
            const auto run=drive_group(world,movement,{{501,goal}},c.speed,400);
            std::cout<<"single tank 10 Cells speed="<<c.speed<<" diagonal="<<c.diagonal<<" frames="<<run.frames<<'\n';
            require(run.all_arrived && run.frames==c.frames,"tank speed differs from the original drive");
        }
    }
    {
        // QA5 freeze: the harvester has its own movement owner. Its tank
        // asks a soldier on the Cell center (bit 0x20) to step aside, so
        // the soldier now holds spot 2 in a new Cell. The Battle movement
        // remembered spot 0 for it. It must read the spot from the Cell,
        // or every later frame fails with "not linked on ground" and no
        // Unit moves.
        rules::IniFile ini;
        ini.parse("[General]\nCloseEnough=2.25\nBlockagePathDelay=60\n[E2]\nSpeed=4\n"
                  "[HARV]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nROT=5\nCrusher=yes\nStrength=1000\n");
        auto world=field(30,20);
        for (int x=6;x<=16;++x) world.cells.at(key(x,9)).cell_flag_100=true,world.cells.at(key(x,11)).cell_flag_100=true;
        world.cells.at(key(11,11)).cell_flag_100=false;
        put(world,1,"E2",sw::Kind::Infantry,1,11,10);put(world,2,"HARV",sw::Kind::Unit,1,5,10);
        PrototypeMovement battle,economy;battle.use_rules(ini);economy.use_rules(ini);
        // A pending order to its own Cell makes the Battle owner remember
        // spot 0. The Battle frame runs only after the harvester passes.
        require(battle.issue(world,1,{11,10},10).status==PrototypeMoveStatus::Complete,"soldier order failed");
        require(economy.issue(world,2,{20,10},4).status==PrototypeMoveStatus::Complete,"harvester order failed");
        std::uint32_t frame=1;
        for (;frame<600 && economy.moving(2);++frame)
            require(economy.tick(world,frame).status==PrototypeMoveStatus::Complete,"harvester frame failed");
        require(same_cell(*unit(world,1).cell,{11,11}) && *world.cells.at(key(11,11)).ground_occupancy_124==4u,
                "harvester did not move the soldier aside to spot 2");
        // The QA5 freeze: this Battle frame failed for every Unit.
        const auto pending=battle.tick(world,frame++);
        if (pending.status!=PrototypeMoveStatus::Complete) std::cerr<<pending.detail<<'\n';
        require(pending.status==PrototypeMoveStatus::Complete,"moved-aside soldier stopped every Unit");
        const auto order=battle.issue(world,1,{25,15},10);
        require(order.status==PrototypeMoveStatus::Complete,"moved-aside soldier refused a new order");
        const auto before=*unit(world,1).position;
        for (const auto stop=frame+400;frame<stop && battle.moving(1);++frame) {
            const auto r=battle.tick(world,frame);
            if (r.status!=PrototypeMoveStatus::Complete) std::cerr<<r.detail<<'\n';
            require(r.status==PrototypeMoveStatus::Complete,"moved-aside soldier stopped every Unit");
        }
        require(!battle.moving(1) && same_cell(*unit(world,1).cell,{25,15}) && unit(world,1).position->x!=before.x,
                "moved-aside soldier did not reach its goal");
        std::cout<<"moved-aside soldier arrived frames="<<frame<<'\n';
    }
    {
        // An incomplete snapshot: a soldier linked in the same Cell has no
        // position. Reading spots must keep the remembered spots and not
        // read the missing position.
        auto world=field(20,12);PrototypeMovement movement;
        soldier(world,1,1,5,5,2);soldier(world,2,1,5,5,3);
        world.entities[1].position.reset();
        const auto order=movement.issue(world,1,{10,5},10);
        require(order.status==PrototypeMoveStatus::Complete,"soldier beside an incomplete soldier refused an order");
    }
    {
        // QA5: 30 Conscripts packed one per Cell (production puts each on
        // the Cell center with bit 0x20) beside 4 Rhinos. One far group
        // order, driven like the Battle loop. Every Unit must leave the
        // block and no frame may fail.
        const auto rules=test_rules();
        auto world=field(70,24);PrototypeMovement movement;movement.use_rules(rules);
        std::vector<std::pair<std::uint32_t,ra2map::Cell>> orders;std::map<std::uint32_t,int> speeds;
        std::uint32_t id=900;
        for (int y=8;y<=12;++y) for (int x=50;x<=55;++x) {
            put(world,++id,"E1",sw::Kind::Infantry,1,x,y);
            orders.push_back({id,{static_cast<std::uint16_t>(x-40),static_cast<std::uint16_t>(y)}});speeds[id]=original_rule_speed(4);
        }
        for (int y=9;y<=12;++y) {
            put(world,++id,"MTNK",sw::Kind::Unit,1,57,y);
            orders.push_back({id,{static_cast<std::uint16_t>(17),static_cast<std::uint16_t>(y)}});speeds[id]=original_rule_speed(7);
        }
        std::map<std::uint32_t,ra2map::Cell> pending;
        for (const auto& [unit_id,goal]:orders) {
            require(movement.issue(world,unit_id,goal,speeds[unit_id]).status==PrototypeMoveStatus::Complete,"packed group order was rejected");
            pending[unit_id]=goal;
        }
        std::uint32_t frame=1;
        for (;frame<=3000 && !pending.empty();++frame) {
            const auto motion=movement.tick(world,frame);
            if (motion.status!=PrototypeMoveStatus::Complete) std::cerr<<"packed group frame "<<frame<<": "<<motion.detail<<'\n';
            require(motion.status==PrototypeMoveStatus::Complete,"packed group frame failed");
            for (const auto b:motion.blocked) movement.cancel(world,b);
            for (const auto a:motion.arrived) pending.erase(a);
            for (const auto& [unit_id,goal]:pending)
                if (frame%15==0 && !movement.moving(unit_id)) movement.issue(world,unit_id,goal,speeds[unit_id]);
        }
        int far=0;
        for (const auto& [unit_id,goal]:orders) if (unit(world,unit_id).cell->x<=30) ++far;
        std::cout<<"packed group frames="<<frame<<" left="<<pending.size()<<" west="<<far<<'\n';
        require(far==int(orders.size()),"packed group did not leave its block");
    }
    original_rules_tests();
    std::cout<<"prototype routed 3 Units around obstacles in "<<frames<<" shared frames; replay and atomic rejection passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
