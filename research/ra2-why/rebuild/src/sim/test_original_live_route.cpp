// Focused test: the recovered original ground path kernel routes playable
// movement. A bound OriginalDistance sends issue() through
// find_original_ground_path on resolved flat no-bridge Cells; a bridge
// flag keeps the explicit legacy fallback.
#include "prototype_movement.h"
#include "../sim/distance.h"
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
        c.ground_occupancy_124=0;c.cell_flag_100=false;c.terrain=original_default_terrain_cell({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
        world.cell_order->push_back(key(x,y));
    }
    return world;
}
void place(sw::Snapshot& world,std::uint32_t id,const char* type,int x,int y) {
    sw::Entity unit;unit.id=id;unit.type=type;unit.kind=sw::Kind::Unit;unit.lifetime=sw::Lifetime::Placed;
    unit.cell=ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
    unit.position=ground_cell_center(x,y);unit.layer=sw::Layer::Ground;unit.next=0;unit.resolved_hp=100;unit.hull_facing=64;
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
    require(movement.issue(world,id,goal,17).status==PrototypeMoveStatus::Complete,"order rejected");
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
    // The kernel routes around a wall through the one gap.
    {
        auto world=open_field(20,20);
        for (int y=1;y<=19;++y) if (y!=10) world.cells.at(key(10,y)).ground_occupancy_124=1; // wall
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"GTNK",2,10);
        const unsigned frames=run_to(world,movement,1,{18,10},600);
        require(frames>0,"the Unit did not arrive around the wall");
        require(same_cell(unit(world,1).cell.value(),{18,10}),"the Unit missed the gap goal");
        std::cout<<"wall detour: arrived in "<<frames<<" frames\n";
    }
    // A bridge flag keeps the explicit legacy fallback: the order still
    // completes, but through the conservative prototype search.
    {
        auto world=open_field(20,20);
        world.cells.at(key(10,10)).cell_flag_100=true;
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"GTNK",2,10);
        const unsigned frames=run_to(world,movement,1,{18,10},600);
        require(frames>0,"the fallback order did not arrive");
        std::cout<<"unsupported bridge domain: legacy fallback arrived in "<<frames<<" frames\n";
    }
    // Two movers cross the field at once without sharing a Cell.
    {
        auto world=open_field(20,20);
        PrototypeMovement movement;movement.use_rules(rules);movement.use_original_path(distance);
        place(world,1,"GTNK",4,5);place(world,2,"GTNK",4,15);
        require(movement.issue(world,1,{16,5},17).status==PrototypeMoveStatus::Complete,"first order rejected");
        require(movement.issue(world,2,{16,15},17).status==PrototypeMoveStatus::Complete,"second order rejected");
        for (std::uint32_t frame=1;frame<=600;++frame) {
            const auto result=movement.tick(world,frame);
            require(result.status==PrototypeMoveStatus::Complete,"parallel frame failed");
            if (movement.moving(1) || movement.moving(2)) {
                require(!same_cell(unit(world,1).cell.value(),unit(world,2).cell.value()),"movers shared a Cell");
                continue;
            }
            require(same_cell(unit(world,1).cell.value(),{16,5}) && same_cell(unit(world,2).cell.value(),{16,15}),
                "both movers did not arrive");
            std::cout<<"parallel crossing: arrived after "<<frame<<" frames with no Cell overlap\n";
            return 0;
        }
        throw std::runtime_error("the parallel movers did not arrive");
    }
}

