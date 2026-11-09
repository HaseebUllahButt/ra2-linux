#include "world.h"
#include <cstdlib>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main(int argc,char** argv) {
    if (argc!=4) return 2;
    const auto seed=static_cast<std::uint32_t>(std::strtoul(argv[1],nullptr,10));
    const int registry_size=std::atoi(argv[2]),color=std::atoi(argv[3]);
    Stage stage(sim::Random(seed).state());
    if (!stage.create_player_house("Player","Americans",5000,color,registry_size)) return 1;
    const auto& snapshot=stage.snapshot();const auto& house=snapshot.houses.at(0);
    std::cout<<*house.constructor_random<<' '<<*house.initial_credits<<' '<<*house.cash<<' '
             <<*house.selected_color<<' '<<*house.palette_color<<' '<<*house.unit_count<<' '
             <<*house.unit_registry_size<<' '<<snapshot.random.first<<' '<<snapshot.random.second;
    for (auto word:snapshot.random.table) std::cout<<' '<<word;
    std::cout<<'\n';
}
