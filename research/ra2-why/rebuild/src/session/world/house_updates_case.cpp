#include "world.h"
#include <algorithm>
#include <cassert>
#include <iostream>
using namespace ra2;
using namespace ra2::session::world;
int main() {
    for (int seed=0;seed<64;++seed) {
        Snapshot input; input.random=sim::Random(42).state();
        House house; house.id=1; house.name="Player"; house.country="Americans";
        house.cash=5000; house.unit_count=3;
        house.buildings_ever_54a0.emplace(); house.aircraft_ever_54d0.emplace();
        house.infantry_ever_54c0.emplace(); house.units_ever_54b0.emplace();
        std::map<std::int32_t,std::int32_t>* arrays[]={&*house.buildings_ever_54a0,&*house.aircraft_ever_54d0,
                        &*house.infantry_ever_54c0,&*house.units_ever_54b0};
        for (int i=0;i<4;++i) for (int j=0;j<(seed*(i+1)+i)%7;++j)
            (*arrays[i])[j]=seed*17+i*5+j+1;
        input.houses.push_back(house); input.creation_order.push_back({CreatedKind::House,1});
        auto missing=input; missing.houses[0].infantry_ever_54c0.reset();
        Stage rejected(missing);
        assert(!rejected.finish_battle_house(1,100));
        assert(rejected.snapshot().houses[0].cash==5000 &&
               rejected.snapshot().houses[0].buildings_ever_54a0==missing.houses[0].buildings_ever_54a0);
        Stage stage(input);
        const int remaining[]={-100,0,7,1000};
        assert(stage.finish_battle_house(1,remaining[seed%4]));
        const auto& result=stage.snapshot(); const auto& updated=result.houses[0];
        assert(updated.cash==5000+std::max(0,remaining[seed%4]) && updated.unit_count==3 && result.random.table==input.random.table &&
               result.random.first==input.random.first && result.random.second==input.random.second);
        std::cout<<*updated.cash<<' '<<updated.buildings_ever_54a0->size()<<' '<<updated.aircraft_ever_54d0->size()<<' '
                 <<updated.infantry_ever_54c0->size()<<' '<<updated.units_ever_54b0->size()<<'\n';
    }
}
