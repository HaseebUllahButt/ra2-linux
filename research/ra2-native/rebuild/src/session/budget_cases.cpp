#include "session.h"
#include <iostream>
#include <vector>
using namespace ra2;
int main() {
    for (int unit_count : {0,1,10}) for (int scenario=0;scenario<64;++scenario) {
        const int tech=scenario%3;
        const unsigned houses=1u+static_cast<unsigned>(scenario%3);
        std::vector<session::StartingUnitType> types;
        for (int i=0;i<5;++i) {
            const bool allowed=(scenario&(1<<i))!=0;
            const unsigned owners=(i&1) ? 2u : 1u;
            types.push_back({allowed,owners,i%3,100*(i+1)+scenario%3,i<3,i==2});
        }
        const auto budget=session::starting_unit_budget(unit_count,tech,houses,types);
        if (!budget) return 1;
        std::cout<<unit_count<<' '<<tech<<' '<<houses<<' '<<*budget<<' '<<types.size();
        for (const auto& type:types)
            std::cout<<' '<<*type.allowed<<' '<<*type.owner_mask<<' '<<*type.tech_level<<' '
                     <<*type.cost<<' '<<type.vehicle<<' '<<*type.base_unit;
        std::cout<<'\n';
    }
}
