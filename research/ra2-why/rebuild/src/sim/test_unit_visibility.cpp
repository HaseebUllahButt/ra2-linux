#include "unit_visibility.h"
#include <iostream>
#include <stdexcept>
using namespace ra2;
using namespace ra2::sim;
namespace sw=ra2::session::world;
namespace {
void require(bool value,const char* text) { if (!value) throw std::runtime_error(text); }
std::uint32_t key(int x,int y) { return std::uint32_t(y)*512+std::uint32_t(x); }
}
int main() try {
    unsigned checks=0;
    for (int h=0;h<4;++h) for (int a:{0,8}) for (int b:{0,8}) for (int mode=0;mode<2;++mode) for (int second=0;second<2;++second) {
        sw::Snapshot world;world.setup_progress=sw::SetupProgress::Complete;world.random=Random(91).state();
        sw::House house;house.id=1;house.name="P";house.country="C";world.houses.push_back(house);
        sw::Entity e;e.id=2;e.house_id=1;e.type="T";e.lifetime=sw::Lifetime::Placed;
        e.position=Coord{20*256+128,20*256+128,h*104};e.cell=ra2map::Cell{20,20};e.layer=sw::Layer::Ground;e.next=0;
        world.entities.push_back(e);world.creation_order={{sw::CreatedKind::House,1},{sw::CreatedKind::Entity,2}};
        auto& target=world.cells[key(20,20)];target.allocated=true;target.terrain=original_default_terrain_cell({20,20});
        target.terrain->floor_world_height=h*104;target.terrain->ramp_index=0;
        std::vector<bool> allocation(512*512);allocation[key(20,20)]=true;
        const int off=h/2+(h&1),x=20-off,y=20-off;
        auto& first=world.cells[key(x,y)];first.allocated=true;first.cell_field_12c=static_cast<std::uint8_t>(a);allocation[key(x,y)]=true;
        if (h&1) {auto& next=world.cells[key(x+1,y+1)];next.allocated=true;next.cell_field_12c=static_cast<std::uint8_t>(b);allocation[key(x+1,y+1)]=true;}
        const bool visible=a==0 && (!(h&1) || b==0);
        UnitVisibilityInputs in;in.allocated=&allocation;in.projection_divisor=104;in.global_mode=bool(mode);
        unsigned second_calls=0,seen_calls=0;
        in.secondary=[&](sw::Snapshot& s,Coord)->std::optional<bool>{++second_calls;s.random.table[0]^=3;return bool(second);};
        in.seen=[&](sw::Snapshot& s,std::uint32_t)->std::optional<bool>{++seen_calls;s.random.table[1]^=7;return false;};
        const auto result=original_unit_insertion_visibility(world,2,key(20,20),in);
        const bool call_seen=!(bool(mode)==false && (visible || bool(second)));
        require(result.status==UnitVisibilityStatus::Complete && result.first_visibility==visible && result.seen_called==call_seen &&
                second_calls==unsigned(!visible) && seen_calls==unsigned(call_seen),"visibility projection or original branch order differs");
        ++checks;
    }
    sw::Snapshot partial;partial.random=Random(91).state();UnitVisibilityInputs missing;
    const auto before=partial.random;
    require(original_unit_insertion_visibility(partial,2,key(20,20),missing).status==UnitVisibilityStatus::NeedsInput &&
            partial.random.table==before.table && partial.random.first==before.first && partial.random.second==before.second,
            "unknown visibility published changes");
    std::cout<<checks<<" checked insertion visibility branches and atomic rejection passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
