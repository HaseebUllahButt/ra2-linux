#include "world.h"
#include <cstdlib>
#include <iostream>
using namespace ra2;
using namespace ra2::session;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=5) return 2;
    const auto bias=static_cast<unsigned>(std::strtoul(argv[1],nullptr,10))&255u;
    const int level=std::atoi(argv[2]);
    const int mark=std::atoi(argv[3]);
    const int flag200=std::atoi(argv[4]);
    const bool real_fixture=std::getenv("RA2_WORLD_MAP_FIXTURE")!=nullptr;
    const int center_x=real_fixture?135:54;
    const int center_y=real_fixture?80:69;
    const int width=real_fixture?100:80;
    if (level!=0 && level!=4) return 2;
    if (mark < -1 || mark > 1) return 2;
    if (flag200 < -1 || flag200 > 1) return 2;
    Stage stage(sim::Random(42).state());
    const int registry_size=real_fixture?55:64;
    const auto house=stage.create_player_house("Player","Americans",5000,0,registry_size);
    if (!house || !stage.create_auxiliary_house(HouseRole::Neutral,registry_size,{}) ||
        !stage.create_auxiliary_house(HouseRole::Special,registry_size,{})) return 1;
    const auto unit=stage.create_unit(*house,"AMCV",1000,0,false); if (!unit) return 1;
    std::map<std::uint32_t,CellState> cells;
    for (int y=center_y-1;y<=center_y+1;++y)
        for (int x=center_x-1;x<=center_x+1;++x) cells[y*512+x].neighbor_count=0;
    constexpr int dx[]={0,1,1,1,0,-1,-1,-1},dy[]={-1,-1,0,1,1,1,0,-1};
    for (int i=0;i<8;++i) cells[(center_y+dy[i])*512+center_x+dx[i]].neighbor_count=static_cast<std::uint8_t>(bias+i);
    auto target=[&](int x,int land,float speed,int category) {
        auto& cell=cells[center_y*512+x];
        cell.heads=weapons::CellContentHeads{};cell.terrain=sim::TerrainCell{};
        cell.terrain->cell={static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(center_y)};
        cell.terrain->height_level=level;cell.terrain->ramp_index=0;cell.terrain->bridge=false;
        cell.terrain->overlay_type_index=-1;cell.terrain->land_type=land;cell.terrain->floor_world_height=level*104;
        cell.unit_land_speed=speed;cell.land_track=speed;cell.usable=true;
        cell.coarse_category=static_cast<std::uint8_t>(category);
        if (flag200>=0) cell.cell_flag_200=flag200!=0;
        if (mark>=0) cell.cell_flag_40000=mark!=0;
        cell.tube_field_54=-1;cell.tube_field_58=-1;cell.tube_field_116=-1;
    };
    target(center_x,0,1,0);target(center_x+2,2,0,4);
    const auto geometry=real_fixture?PlacementGeometry{100,100,{5,5,90,89}}:
                                     PlacementGeometry{width,width,{2,2,76,72}};
    if (!stage.install_base_cells(geometry,std::move(cells),0,{})) return 1;
    BaseUnitProfile profile;
    profile.restricted_land=-1;profile.movement_mode=-1;profile.placement_mode=2;
    profile.speed_type=1;
    profile.level_height=104;
    profile.type_flag_aa0=false;profile.unit_flag_32c=false;profile.linked_team=false;
    profile.type_flag_205=true;profile.unit_flag_32a=false;profile.type_flag_bb0=false;profile.type_flag_bb1=false;
    for (const auto point:{StartPoint{static_cast<std::int16_t>(center_x+2),static_cast<std::int16_t>(center_y)},
                          StartPoint{static_cast<std::int16_t>(center_x),static_cast<std::int16_t>(center_y)}}) {
        const auto result=stage.place_base_unit(*unit,point,32,profile);
        if (result==PlacementResult::Unresolved) return 1;
        const auto& s=stage.snapshot();const auto& u=s.entities.at(0);const auto& h=s.houses.at(0);
        const auto count=h.placed_unit_counts->find(0);
        std::cout<<(result==PlacementResult::Placed?1:0)<<' '<<int(*u.on_map)<<' '
                 <<s.cells.at(point.y*512+point.x).heads->ground_head<<' '
                 <<(count==h.placed_unit_counts->end()?0:count->second)<<' '<<*u.next<<' '
                 <<*u.hull_facing<<' '<<*u.turret_facing<<' '
                 <<(u.position?u.position->z:-1)<<' '<<(u.height_byte?int(*u.height_byte):-1);
        for (int i=0;i<8;++i) std::cout<<' '<<int(*s.cells.at((center_y+dy[i])*512+center_x+dx[i]).neighbor_count);
        std::cout<<' '<<s.random.first<<' '<<s.random.second;
        for (auto word:s.random.table) std::cout<<' '<<word;
        std::cout<<'\n';
    }
}
