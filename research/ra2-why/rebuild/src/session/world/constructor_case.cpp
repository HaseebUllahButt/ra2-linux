#include "world.h"
#include <cstdlib>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc<4 || argc>6) return 2;
    const auto seed=static_cast<std::uint32_t>(std::strtoul(argv[1],nullptr,10));
    const int type_index=std::atoi(argv[2]);
    const bool excluded=std::atoi(argv[3])!=0;
    const int registry_size=argc>=5?std::atoi(argv[4]):512;
    const bool auxiliaries=argc==6 && std::string(argv[5])=="--aux";
    Stage stage(sim::Random(seed).state());
    const auto house=stage.create_player_house("Player","Americans",5000,0,registry_size);
    if (auxiliaries && (!stage.create_auxiliary_house(HouseRole::Neutral,registry_size,{}) ||
                        !stage.create_auxiliary_house(HouseRole::Special,registry_size,{}))) return 1;
    if (!house || !stage.create_unit(*house,"AMCV",1000,type_index,excluded)) return 1;
    const auto& snapshot=stage.snapshot();
    const auto& unit=snapshot.entities.at(0);
    const auto& owner=snapshot.houses.at(0);
    const auto count=[type_index](const auto& values) {
        const auto it=values->find(type_index);
        return it==values->end()?0:it->second;
    };
    std::cout<<*unit.constructor_random<<' '<<*unit.next<<' '<<int(*unit.on_map)<<' '<<*unit.resolved_hp<<' '
             <<*unit.hull_facing<<' '<<*unit.turret_facing<<' '
             <<*owner.unit_count<<' '<<count(owner.unit_type_counts)<<' '<<count(owner.unit_fixed_counts)<<' '
             <<snapshot.random.first<<' '<<snapshot.random.second;
    for (auto word:snapshot.random.table) std::cout<<' '<<word;
    std::cout<<'\n';
}
