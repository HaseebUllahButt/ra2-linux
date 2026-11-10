// Deterministic movement scenes. Fixed snapshots, no randomness.
// Scenes: a tank across open ground, 8 tanks through a 2-cell gap with
// distinct goals, and an infantry squad with distinct subcells.
// Reference numbers: reference/REFERENCE.md "Unit speed". Measured steady
// speeds per game frame at Game Speed 3, clear ground: Grizzly 0.105,
// Rhino 0.093, GI 0.039, Conscript about 0.036 cells/frame. The printed
// totals include start, stop and turning; the steady rate does not.
#include "prototype_movement.h"
#include "drive.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
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
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.cell_flag_40000=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
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
// any window of the run, so start, stop and turning stay out of it. The
// distance is straight-line, so diagonal paths measure the same rate.
// In original mode each scene must be routed by the kernel, and the
// legacy search must not answer.
void check_kernel_ran(const PrototypeRouteTrace& before,const PrototypeRouteTrace& after) {
    require(after.kernel_routes>before.kernel_routes,"the original kernel did not route the scene");
    require(after.legacy_routes==before.legacy_routes,"the legacy search answered in original mode");
}
double steady_cells_per_frame(const std::vector<std::pair<int,int>>& positions,unsigned window,bool axis_only=false) {
    double best=0.0;
    for (std::size_t i=window;i<positions.size();++i) {
        const auto dx=positions[i].first-positions[i-window].first;
        const auto dy=positions[i].second-positions[i-window].second;
        if (axis_only && dx!=0 && dy!=0) continue; // Skip diagonal segments.
        best=std::max(best,std::sqrt(double(dx)*dx+double(dy)*dy)/256.0/window);
    }
    return best;
}
}

int main(int argc,char** argv) try {
    if (argc>2) { std::cerr<<"usage: test_movement_scenes [game.exe]\n"; return 2; }
    const auto rules=test_rules();
    // With a game.exe argument the scenes run through the original path
    // kernel (use_original_path). Without one they keep the legacy search.
    // Flat known fixtures: every Cell resolves Cell +40000 as clear.
    std::optional<sim::OriginalDistance> distance;
    if (argc==2) distance=sim::OriginalDistance::from_executable(argv[1]);

    // Scene 1: one tank (MTNK) across 20 Cells of open ground.
    // Measured (REFERENCE.md): Grizzly steady 0.105 cells/frame; 11 Cells
    // took 5.06 s total including start, stop and one turn.
    {
        auto world=field(32,12);PrototypeMovement movement;movement.use_rules(rules);
        if (distance) movement.use_original_path(*distance);
        const auto trace0=movement.route_trace();
        put(world,901,"MTNK",sw::Kind::Unit,5,6,300);
        const auto speed=movement.profile(unit(world,901)).speed;
        require(movement.issue(world,901,{25,6},speed).status==PrototypeMoveStatus::Complete,"tank order refused");
        unsigned frames=0;std::vector<std::pair<int,int>> track;
        for (std::uint32_t frame=1;frame<=400;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 1 frame failed");
            check_no_overlap(world);check_subcells(world,movement);
            track.push_back({unit(world,901).position->x,unit(world,901).position->y});
            if (!movement.moving(901)) { frames=frame;break; }
        }
        const auto steady=steady_cells_per_frame(track,50);
        const auto error=(steady-0.105)/0.105;
        std::cout<<"scene 1 open ground frames="<<frames
                 <<" steady="<<steady<<" cells/frame measured=0.105 error="<<error<<"\n";
        require(frames && frames<=400,"tank did not cross open ground in 400 frames");
        require(std::abs(error)<0.05,"steady tank speed off the measured 0.105 cells/frame");
        require(same_cell(*unit(world,901).cell,{25,6}),"tank missed its goal");
        if (distance) check_kernel_ran(trace0,movement.route_trace());
    }

    // Scene 2: 8 tanks (MTNK) through a 2-cell gap, each with a distinct
    // goal. Measured steady rate per tank: 0.105 cells/frame (Grizzly).
    {
        auto world=field(40,20);PrototypeMovement movement;movement.use_rules(rules);
        if (distance) movement.use_original_path(*distance);
        const auto trace0=movement.route_trace();
        for (int y=1;y<=20;++y) if (y!=9 && y!=10) world.cells.at(key(11,y)).ground_occupancy_124=1;
        const ra2map::Cell goals[8]={{20,2},{20,4},{20,6},{20,8},{20,12},{20,14},{20,16},{20,18}};
        for (unsigned i=0;i<8;++i) put(world,910+i,"MTNK",sw::Kind::Unit,static_cast<int>(2+i/4),static_cast<int>(2+i%4*5)+2,300);
        double worst_error=0.0;unsigned frames=0;
        std::map<std::uint32_t,std::vector<std::pair<int,int>>> tracks;
        for (unsigned i=0;i<8;++i)
            require(movement.issue(world,910+i,goals[i],movement.profile(unit(world,910+i)).speed).status==PrototypeMoveStatus::Complete,"a tank in the gap was refused");
        for (std::uint32_t frame=1;frame<=1200;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 2 frame failed");
            check_no_overlap(world);check_subcells(world,movement);
            for (unsigned i=0;i<8;++i) tracks[910+i].push_back({unit(world,910+i).position->x,unit(world,910+i).position->y});
            if (!movement.active_count()) { frames=frame;break; }
        }
        for (unsigned i=0;i<8;++i) {
            const auto steady=steady_cells_per_frame(tracks[910+i],50);
            worst_error=std::max(worst_error,std::abs((steady-0.105)/0.105));
        }
        std::cout<<"scene 2 gap frames="<<frames<<" worst steady error="<<worst_error<<"\n";
        require(frames && frames<=1200,"8 tanks did not clear the gap in 1200 frames");
        require(worst_error<0.10,"a queued tank lost too much steady speed against the measured 0.105");
        for (unsigned i=0;i<8;++i) require(same_cell(*unit(world,910+i).cell,goals[i]),"a tank missed its distinct goal");
        check_no_overlap(world);
        if (distance) check_kernel_ran(trace0,movement.route_trace());
    }

    // Scene 3: an infantry squad (E1 and E2) with distinct subcells.
    // Measured (REFERENCE.md): GI steady 0.80 cells/second, 0.039 cells
    // per game frame; 7 Cells took 8.9 s total. Conscript 0.72-0.78
    // (noisy), about 0.036 cells per game frame.
    {
        auto world=field(32,12);PrototypeMovement movement;movement.use_rules(rules);
        if (distance) movement.use_original_path(*distance);
        const auto trace0=movement.route_trace();
        soldier(world,930,"E1",5,6,2);soldier(world,931,"E1",5,6,3);soldier(world,932,"E2",5,6,4);
        soldier(world,933,"E1",5,7,2);soldier(world,934,"E2",5,7,3);soldier(world,935,"E2",5,7,4);
        const ra2map::Cell goals[6]={{20,2},{20,4},{20,6},{20,8},{20,10},{20,12}};
        for (unsigned i=0;i<6;++i)
            require(movement.issue(world,930+i,goals[i],movement.profile(unit(world,930+i)).speed).status==PrototypeMoveStatus::Complete,"a soldier was refused");
        check_subcells(world,movement);
        unsigned frames=0;double e1_error=0.0,e2_error=0.0;
        std::map<std::uint32_t,std::vector<std::pair<int,int>>> tracks;
        for (std::uint32_t frame=1;frame<=600;++frame) {
            require(movement.tick(world,frame).status==PrototypeMoveStatus::Complete,"scene 3 frame failed");
            check_no_overlap(world);check_subcells(world,movement);
            for (unsigned i=0;i<6;++i) tracks[930+i].push_back({unit(world,930+i).position->x,unit(world,930+i).position->y});
            if (!movement.active_count()) { frames=frame;break; }
        }
        for (unsigned i=0;i<6;++i) {
            const auto steady=steady_cells_per_frame(tracks[930+i],50,true);
            // Bucket by the entity type, not by order index.
            const auto is_e1=unit(world,930+i).type=="E1";
            const auto measured=is_e1?0.039:0.036;
            const auto error=(steady-measured)/measured;
            if (is_e1) e1_error=std::max(e1_error,std::abs(error)); else e2_error=std::max(e2_error,std::abs(error));
        }
        std::cout<<"scene 3 squad frames="<<frames<<" worst E1 error="<<e1_error
                 <<" worst E2 error="<<e2_error<<" (measured 0.039 and 0.036 cells/frame)\n";
        require(frames && frames<=600,"the squad did not arrive in 600 frames");
        require(e1_error<0.05,"E1 steady speed off the measured 0.039 cells/frame");
        require(e2_error<0.15,"E2 steady speed off the noisy measured 0.036 cells/frame");
        for (unsigned i=0;i<6;++i) require(same_cell(*unit(world,930+i).cell,goals[i]),"a soldier missed its goal");
        check_subcells(world,movement);
        if (distance) check_kernel_ran(trace0,movement.route_trace());
    }

    std::cout<<"movement scenes ok\n";
} catch (const std::exception& e) {
    std::cerr<<"FAIL: "<<e.what()<<'\n';
    return 1;
}
