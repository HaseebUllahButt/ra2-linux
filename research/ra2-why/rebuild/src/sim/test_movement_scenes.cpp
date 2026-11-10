// Deterministic movement scenes. Fixed snapshots, no randomness.
// Scenes: a tank across open ground, 8 tanks through a 2-cell gap with
// distinct goals, and an infantry squad with distinct subcells.
// Reference numbers: reference/REFERENCE.md "Unit speed".
// Measured steady speeds (Game Speed 3, clear ground):
//   Grizzly 0.105 cells/frame, Rhino 0.093, GI 0.039, Conscript 0.036.
// The printed totals include start, stop and turning; the steady rate does not.
#include "prototype_movement.h"
#include "drive.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x,int y) { return std::uint32_t(y)*512+std::uint32_t(x); }

rules::IniFile test_rules() {
    // Type values from the real RULES.INI: MTNK Speed=7 ROT=5 Strength=300,
    // HTNK Speed=6 ROT=5 Strength=400, E1/E2 Speed=4 Strength=125 with the
    // walk locomotor {4A582744-...}. Clear ground only.
    rules::IniFile ini;
    ini.parse(
        "[General]\nCloseEnough=2.25\nBlockagePathDelay=60\nTrackedUphill=1.0\nTrackedDownhill=1.2\n"
        "[AudioVisual]\nConditionYellow=50%\n"
        "[Clear]\nFoot=100%\nTrack=100%\nWheel=100%\n"
        "[MTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=5\nAccelerates=false\nCrusher=yes\nStrength=300\n"
        "[HTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=6\nROT=5\nAccelerates=false\nStrength=400\n"
        "[E1]\nLocomotor={4A582744-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nStrength=125\nCrushable=yes\n"
        "[E2]\nLocomotor={4A582744-9839-11d1-B709-00A024DDAFD1}\nSpeed=4\nStrength=125\nCrushable=yes\n");
    return ini;
}

sw::Snapshot field(int width,int height) {
    sw::Snapshot world;world.setup_progress=sw::SetupProgress::PrototypeComplete;world.random=Random(24).state();
    world.cell_order=std::vector<std::uint32_t>{};
    sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
    world.creation_order.push_back({sw::CreatedKind::House,1});
    for (int y=1;y<=height;++y) for (int x=1;x<=width;++x) {
        auto& c=world.cells[key(x,y)];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x,y));
    }
    return world;
}

// Full actual type strength, so no tank sits at ConditionYellow and the
// 0.75 damaged-speed rule stays out of the scenes.
void put(sw::Snapshot& world,std::uint32_t id,const char* type,sw::Kind kind,int x,int y,int strength,int facing=64) {
    sw::Entity unit;unit.id=id;unit.house_id=1;unit.type=type;unit.kind=kind;unit.lifetime=sw::Lifetime::Placed;
    unit.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    unit.position=ground_cell_center(x,y);unit.layer=sw::Layer::Ground;unit.next=0;
    unit.resolved_hp=strength;unit.maximum_hp=strength;
    unit.hull_facing=facing;
    world.entities.push_back(unit);world.creation_order.push_back({sw::CreatedKind::Entity,unit.id});
    auto& c=world.cells.at(key(x,y));c.heads->ground_head=id;c.ground_occupancy_124=32;
}

bool same_cell(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
const sw::Entity& unit(const sw::Snapshot& w,std::uint32_t id) {
    for (const auto& e:w.entities) if (e.id==id) return e;
    throw std::runtime_error("missing Unit");
}

// No two vehicles share a Cell, and no two centers sit on the same spot.
void check_no_overlap(const sw::Snapshot& world) {
    for (std::size_t i=0;i<world.entities.size();++i) for (std::size_t j=i+1;j<world.entities.size();++j) {
        const auto& a=world.entities[i];const auto& b=world.entities[j];
        if (a.kind!=sw::Kind::Unit || b.kind!=sw::Kind::Unit) continue; // Infantry share a Cell on spots.
        if (same_cell(*a.cell,*b.cell))
            std::cerr<<"vehicle overlap: ids "<<a.id<<" "<<b.id<<" cell "<<a.cell->x<<","<<a.cell->y
                     <<" pos "<<a.position->x<<","<<a.position->y<<" | "<<b.position->x<<","<<b.position->y<<"\n";
        require(!same_cell(*a.cell,*b.cell),"two vehicles shared a Cell");
        require(a.position->x!=b.position->x || a.position->y!=b.position->y || a.position->z!=b.position->z,
                "two vehicles overlapped on one spot");
    }
}

// A soldier on spot 2..4 of a Cell, linked as the chain head.
void soldier(sw::Snapshot& world,std::uint32_t id,const char* type,int x,int y,int spot) {
    static constexpr int sx[5]={128,64,192,64,192},sy[5]={128,64,64,192,192};
    sw::Entity e;e.id=id;e.house_id=1;e.type=type;e.kind=sw::Kind::Infantry;e.lifetime=sw::Lifetime::Placed;
    e.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    e.position=Coord{x*256+sx[spot],y*256+sy[spot],0};e.layer=sw::Layer::Ground;e.next=0;e.resolved_hp=125;e.maximum_hp=125;e.hull_facing=64;
    auto& c=world.cells.at(key(x,y));e.next=c.heads->ground_head;c.heads->ground_head=id;*c.ground_occupancy_124|=1u<<spot;
    world.entities.push_back(e);world.creation_order.push_back({sw::CreatedKind::Entity,id});
}

// Infantry spots stay distinct every frame: occupancy bits, the ground
// chain and (for soldiers that are not walking) the exact subcell spot.
void check_subcells(const sw::Snapshot& world,const PrototypeMovement& movement) {
    std::map<std::pair<int,int>,int> spots;
    for (const auto& e:world.entities) {
        if (e.kind!=sw::Kind::Infantry || movement.moving(e.id)) continue;
        const auto s=std::make_pair(e.position->x,e.position->y);
        if (++spots[s]>1)
            std::cerr<<"subcell clash id "<<e.id<<" cell "<<e.cell->x<<","<<e.cell->y
                     <<" pos "<<e.position->x<<","<<e.position->y<<"\n";
        require(spots[s]==1,"two infantry stood on one subcell");
    }
    // Every Cell: the ground chain holds exactly the Units on that Cell and
    // each infantry spot bit is set at most once.
    for (const auto& [k,c]:world.cells) {
        std::map<std::uint32_t,bool> seen;
        auto id=c.heads?c.heads->ground_head:0u;
        for (unsigned hop=0;id; ++hop) {
            require(hop<64u,"ground chain has a cycle");
            require(!seen[id],"ground chain visits one Unit twice");seen[id]=true;
            const sw::Entity* e=nullptr;
            for (const auto& x:world.entities) if (x.id==id) { e=&x;break; }
            require(e,"ground chain names a missing Unit");
            id=e->next.value_or(0);
        }
        for (const auto& e:world.entities) {
            const auto in_cell=e.cell && key(e.cell->x,e.cell->y)==k && e.layer==sw::Layer::Ground;
            require(seen[e.id]==static_cast<bool>(in_cell),"ground chain does not match the Units on one Cell");
        }
        const auto bits=*c.ground_occupancy_124;
        const auto walkers=std::count_if(world.entities.begin(),world.entities.end(),
                [&](const sw::Entity& e){ return e.kind==sw::Kind::Infantry && key(e.cell->x,e.cell->y)==k; });
        const auto vehicles=std::count_if(world.entities.begin(),world.entities.end(),
                [&](const sw::Entity& e){ return e.kind==sw::Kind::Unit && key(e.cell->x,e.cell->y)==k; });
        require(__builtin_popcount(bits&0x1Eu)==static_cast<int>(walkers),"two infantry shared one subcell spot");
        require(((bits>>5)&1u)==(vehicles?1u:0u),"vehicle whole-Cell bit does not match the Cell content");
    }
}

// Steady displacement after warmup: the largest cells-per-frame rate over
// any window of the run, so start, stop and turning stay out of it.
double steady_cells_per_frame(const std::vector<std::pair<int,int>>& positions,unsigned window) {
    double best=0.0;
    for (std::size_t i=window;i<positions.size();++i) {
        const auto dx=std::abs(positions[i].first-positions[i-window].first);
        const auto dy=std::abs(positions[i].second-positions[i-window].second);
        best=std::max(best,(double(dx)+dy)/256.0/window);
    }
    return best;
}
}

int main() try {
    const auto rules=test_rules();

    // Scene 1: one tank (MTNK) across 20 Cells of open ground.
    // Measured: Grizzly steady 0.105 cells/frame; 11 Cells took 5.06 s
    // (about 105 frames) including start, stop and one turn.
    {
        auto world=field(32,12);PrototypeMovement movement;movement.use_rules(rules);
        put(world,901,"MTNK",sw::Kind::Unit,5,6,300);
        require(movement.issue(world,901,{25,6},original_rule_speed(7)).status==PrototypeMoveStatus::Complete,"tank order refused");
        unsigned frames=0;std::vector<std::pair<int,int>> track;
        for (std::uint32_t frame=1;frame<=400;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 1 frame failed");
            check_no_overlap(world);
            track.push_back({unit(world,901).position->x,unit(world,901).position->y});
            if (!movement.moving(901)) { frames=frame;break; }
        }
        const auto steady=steady_cells_per_frame(track,50);
        std::cout<<"scene 1 open ground frames="<<frames<<" steady="<<steady<<" cells/frame\n";
        // Measured: Grizzly steady 0.105 cells/frame at 20.75 screen fps
        // (2.17 cells/s); 11 Cells took 5.06 s total including start, stop
        // and turning. The steady measure skips warmup, so it matches the
        // measured rate; the frame total does not.
        require(frames && frames<=400,"tank did not cross open ground in 400 frames");
        require(steady>0.09 && steady<0.12,"steady tank speed far from the measured 0.105 cells/frame");
        require(same_cell(*unit(world,901).cell,{25,6}),"tank missed its goal");
    }

    // Scene 2: 8 tanks through a 2-cell gap, each with a distinct goal.
    {
        auto world=field(40,20);PrototypeMovement movement;movement.use_rules(rules);
        for (int y=1;y<=20;++y) if (y!=9 && y!=10) world.cells.at(key(11,y)).ground_occupancy_124=1;
        const ra2map::Cell goals[8]={{20,2},{20,4},{20,6},{20,8},{20,12},{20,14},{20,16},{20,18}};
        for (unsigned i=0;i<8;++i) put(world,910+i,"MTNK",sw::Kind::Unit,static_cast<int>(2+i/4),static_cast<int>(2+i%4*5)+2,300);
        for (unsigned i=0;i<8;++i)
            require(movement.issue(world,910+i,goals[i],original_rule_speed(7)).status==PrototypeMoveStatus::Complete,"a tank in the gap was refused");
        unsigned frames=0;
        for (std::uint32_t frame=1;frame<=1200;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 2 frame failed");
            check_no_overlap(world);
            if (!movement.active_count()) { frames=frame;break; }
        }
        std::cout<<"scene 2 gap frames="<<frames<<"\n";
        require(frames && frames<=1200,"8 tanks did not clear the gap in 1200 frames");
        for (unsigned i=0;i<8;++i) require(same_cell(*unit(world,910+i).cell,goals[i]),"a tank missed its distinct goal");
        check_no_overlap(world);
    }

    // Scene 3: an infantry squad (E1 and E2) with distinct subcells.
    // Measured, not asserted: GI steady 0.80 cells/s (0.039 cells per
    // screen frame); 7 Cells took 8.9 s total. Conscript 0.72-0.78
    // (noisy). The sim runs rule-speed leptons per logic frame.
    {
        auto world=field(32,12);PrototypeMovement movement;movement.use_rules(rules);
        soldier(world,930,"E1",5,6,2);soldier(world,931,"E1",5,6,3);soldier(world,932,"E2",5,6,4);
        soldier(world,933,"E1",5,7,2);soldier(world,934,"E2",5,7,3);soldier(world,935,"E2",5,7,4);
        const ra2map::Cell goals[6]={{20,2},{20,4},{20,6},{20,8},{20,10},{20,12}};
        for (unsigned i=0;i<6;++i)
            require(movement.issue(world,930+i,goals[i],original_rule_speed(4)).status==PrototypeMoveStatus::Complete,"a soldier was refused");
        check_subcells(world,movement);
        unsigned frames=0;
        for (std::uint32_t frame=1;frame<=600;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 3 frame failed");
            check_no_overlap(world);check_subcells(world,movement);
            if (!movement.active_count()) { frames=frame;break; }
        }
        std::cout<<"scene 3 squad frames="<<frames<<"\n";
        require(frames && frames<=600,"the squad did not arrive in 600 frames");
        for (unsigned i=0;i<6;++i) require(same_cell(*unit(world,930+i).cell,goals[i]),"a soldier missed its goal");
        check_subcells(world,movement);
    }

    std::cout<<"movement scenes ok\n";
} catch (const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
