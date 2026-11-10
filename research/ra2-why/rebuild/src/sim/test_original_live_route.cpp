// Focused test: the recovered original ground path kernel routes playable
// movement. A bound OriginalDistance sends issue() through
// find_original_ground_path on resolved flat no-bridge Cells; a bridge
// flag keeps the explicit legacy fallback.
#include "prototype_movement.h"
#include "../sim/distance.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;

void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x,int y) { return std::uint32_t(y)*512+std::uint32_t(x); }

rules::IniFile test_rules() {
    rules::IniFile ini;
    ini.parse(
        "[General]\nCloseEnough=2.25\nBlockagePathDelay=60\nTrackedUphill=1.0\nTrackedDownhill=1.2\n"
        "[AudioVisual]\nConditionYellow=50%\n"
        "[Clear]\nFoot=100%\nTrack=100%\nWheel=100%\n"
        "[MTNK]\nLocomotor={4A582741-9839-11d1-B709-00A024DDAFD1}\nSpeed=7\nROT=5\nAccelerates=false\nCrusher=yes\nStrength=300\n");
    return ini;
}

sw::Snapshot open_field(int width,int height) {
    sw::Snapshot world;world.setup_progress=sw::SetupProgress::PrototypeComplete;world.random=Random(24).state();world.cell_order=std::vector<std::uint32_t>{};
    sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
    world.creation_order.push_back({sw::CreatedKind::House,1});
    for (int y=1;y<=height;++y) for (int x=1;x<=width;++x) {
        auto& c=world.cells[key(x,y)];c.allocated=true;c.heads=weapons::CellContentHeads{};
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.cell_flag_40000=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x,y));
    }
    return world;
}
void place(sw::Snapshot& world,std::uint32_t id,const char* type,int x,int y) {
    sw::Entity unit;unit.id=id;unit.house_id=1;unit.type=type;unit.kind=sw::Kind::Unit;unit.lifetime=sw::Lifetime::Placed;
    unit.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    unit.position=ground_cell_center(x,y);unit.layer=sw::Layer::Ground;unit.next=0;
    unit.resolved_hp=300;unit.maximum_hp=300; // MTNK full 300 HP.
    unit.hull_facing=64;
    world.entities.push_back(unit);world.creation_order.push_back({sw::CreatedKind::Entity,id});
    auto& c=world.cells.at(key(x,y));c.heads->ground_head=id;c.ground_occupancy_124=32;
}
bool same_cell(ra2map::Cell a,ra2map::Cell b) { return a.x==b.x && a.y==b.y; }
const sw::Entity& unit(const sw::Snapshot& world,std::uint32_t id) {
    for (const auto& e:world.entities) if (e.id==id) return e;
    throw std::runtime_error("missing Unit");
}
// Frames until the Unit arrives, or zero.
unsigned run_to(sw::Snapshot& world,PrototypeMovement& movement,std::uint32_t id,ra2map::Cell goal,unsigned limit) {
    require(movement.issue(world,id,goal,movement.profile(unit(world,id)).speed).status==PrototypeMoveStatus::Complete,"order rejected");
    for (std::uint32_t frame=1;frame<=limit;++frame) {
        const auto result=movement.tick(world,frame);
        require(result.status==PrototypeMoveStatus::Complete,"frame failed");
        for (const auto arrived:result.arrived) if (arrived==id) return frame;
    }
    return 0;
}

int main(int argc,char** argv) {
    if (argc!=2) { std::cerr<<"usage: test_original_live_route <game.exe>\n"; return 2; }
    const auto rules=test_rules();
    const auto distance=sim::OriginalDistance::from_executable(argv[1]);
    // The kernel routes around a closed wall; the straight line is a wall.
    {
        auto world=open_field(20,22);
        for (int y=1;y<=20;++y) world.cells.at(key(10,y)).ground_occupancy_124=1; // closed wall
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"MTNK",2,10);
        const unsigned frames=run_to(world,movement,1,{18,10},900);
        require(frames>0,"the Unit did not arrive around the wall");
        require(same_cell(unit(world,1).cell.value(),{18,10}),"the Unit missed the goal Cell");
        require(frames>16,"the route was straight through the wall, not a detour");
        require(movement.route_trace().kernel_routes>=1,"the original kernel did not route the detour");
        require(movement.route_trace().legacy_routes==0,"the legacy search answered the detour");
        std::cout<<"wall detour: arrived in "<<frames<<" frames\n";
    }
    // A goal inside a closed box has no route: the kernel says NoPath, the
    // Unit settles on the near side and never arrives, and the legacy
    // search never answers the enclosed goal.
    {
        auto world=open_field(20,20);
        for (int x=7;x<=13;++x) for (int y=7;y<=13;++y)
            if (x==7 || x==13 || y==7 || y==13) world.cells.at(key(x,y)).ground_occupancy_124=1;
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"MTNK",2,10);
        require(movement.issue(world,1,{10,10},movement.profile(unit(world,1)).speed).status==PrototypeMoveStatus::Complete,
            "the near-side order was rejected");
        bool blocked_seen=false;
        for (std::uint32_t frame=1;frame<=300;++frame) {
            const auto result=movement.tick(world,frame);
            require(result.status==PrototypeMoveStatus::Complete,"enclosed-goal frame failed");
            blocked_seen|=std::find(result.blocked.begin(),result.blocked.end(),1u)!=result.blocked.end();
            for (const auto arrived:result.arrived) require(arrived!=1,"the Unit arrived at an enclosed goal");
        }
        require(blocked_seen,"the enclosed goal never reported Blocked");
        const auto at=unit(world,1).cell.value();
        require(!(at.x>=7 && at.x<=13 && at.y>=7 && at.y<=13),"the Unit entered the closed box");
        require(movement.route_trace().kernel_no_path>=1,"the kernel did not report NoPath");
        require(movement.route_trace().legacy_routes==0,"the legacy search answered the enclosed goal");
        std::cout<<"enclosed goal: kernel NoPath, Unit held outside the box\n";
    }
    // A Cell whose height inputs are missing or out of range rejects the
    // order before any move. Each case removes or breaks one optional.
    {
        const auto break_height=[](sw::Snapshot& world,const char* which) {
            auto& terrain=*world.cells.at(key(18,10)).terrain;
            if (std::string_view(which)=="height_level") terrain.height_level=std::nullopt;
            else if (std::string_view(which)=="ramp_index") terrain.ramp_index=std::nullopt;
            else if (std::string_view(which)=="bridge") terrain.bridge=std::nullopt;
            else if (std::string_view(which)=="level_range") terrain.height_level=300;
            else terrain.ramp_index=256;
        };
        for (const char* which:{"height_level","ramp_index","bridge","level_range","ramp_range"}) {
            auto world=open_field(20,20);
            break_height(world,which);
            PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
            place(world,1,"MTNK",2,10);
            require(movement.issue(world,1,{18,10},movement.profile(unit(world,1)).speed).status==PrototypeMoveStatus::NeedsInput,
                "a broken height input did not reject the order");
            require(!movement.moving(1),"a rejected order kept a route");
            require(movement.route_trace().kernel_routes==0,"a broken height input still produced a route");
            std::cout<<"height input "<<which<<": order rejected with no route\n";
        }
    }
    // start == goal is a complete empty route without a search.
    {
        auto world=open_field(20,20);
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"MTNK",10,10);
        require(movement.issue(world,1,{10,10},movement.profile(unit(world,1)).speed).status==PrototypeMoveStatus::Complete,
            "start == goal was not Complete");
        const unsigned frames=run_to(world,movement,1,{10,10},10);
        require(frames>0,"start == goal did not arrive");
        require(same_cell(unit(world,1).cell.value(),{10,10}),"start == goal moved the Unit");
        require(movement.route_trace().kernel_routes==0 && movement.route_trace().legacy_routes==0,
            "start == goal ran a search");
        std::cout<<"start == goal: complete empty route\n";
    }
    // A true terrain bridge is an explicit unsupported domain for the
    // kernel: the legacy search completes the order instead.
    {
        auto world=open_field(20,20);
        world.cells.at(key(10,10)).terrain->bridge=true;
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"MTNK",2,10);
        const unsigned frames=run_to(world,movement,1,{18,10},600);
        require(frames>0,"the bridge fallback order did not arrive");
        require(same_cell(unit(world,1).cell.value(),{18,10}),"the Unit missed the goal Cell");
        require(movement.route_trace().kernel_routes==0,"the kernel routed through a bridge");
        require(movement.route_trace().legacy_routes>=1,"the legacy search did not take the bridge fallback");
        std::cout<<"unsupported bridge domain: legacy fallback arrived in "<<frames<<" frames\n";
    }
    // Two movers with conflicting routes cross the field. They never share
    // a Cell, and each arrives on its exact goal Cell.
    {
        auto world=open_field(20,20);
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"MTNK",2,10);place(world,2,"MTNK",18,2);
        require(movement.issue(world,1,{18,10},movement.profile(unit(world,1)).speed).status==PrototypeMoveStatus::Complete,"first order rejected");
        require(movement.issue(world,2,{2,18},movement.profile(unit(world,2)).speed).status==PrototypeMoveStatus::Complete,"second order rejected");
        for (std::uint32_t frame=1;frame<=1200;++frame) {
            const auto result=movement.tick(world,frame);
            require(result.status==PrototypeMoveStatus::Complete,"conflict frame failed");
            if (movement.moving(1) || movement.moving(2)) {
                require(!same_cell(unit(world,1).cell.value(),unit(world,2).cell.value()),"conflicting movers shared a Cell");
                continue;
            }
            require(same_cell(unit(world,1).cell.value(),{18,10}) && same_cell(unit(world,2).cell.value(),{2,18}),
                "a conflicting mover missed its goal Cell");
            require(movement.route_trace().kernel_routes>=2,"the original kernel did not route both movers");
            std::cout<<"conflicting crossing: arrived after "<<frame<<" frames with no Cell overlap\n";
            return 0;
        }
        throw std::runtime_error("the conflicting movers did not arrive");
    }
}
