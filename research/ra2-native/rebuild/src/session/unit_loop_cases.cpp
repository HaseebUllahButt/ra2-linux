#include "session.h"
#include "../sim/sim.h"
#include <iostream>
#include <vector>
using namespace ra2;
int main() {
    const std::vector<session::StartingUnitType> types{
        {true,1,1,90,true,false},{true,1,1,110,true,false},{true,1,1,125,true,false},
        {true,1,1,30,false,false},{true,1,1,40,false,false},{true,1,1,60,false,false}};
    for (int budget : {60,120,500}) for (int mode : {0,1,2}) for (unsigned seed : {0u,42u,987654u}) {
        sim::Random random(seed);
        std::vector<std::size_t> attempts;
        std::vector<int> results;
        auto plan=session::generate_starting_units(budget,1,1,types,random,
            [&](std::size_t type,sim::Random&) -> std::optional<session::UnitAttemptResult> {
                attempts.push_back(type);
                const bool success=mode==0 || (mode==1 && attempts.size()%2==0);
                results.push_back(success);
                return session::UnitAttemptResult{success,{static_cast<std::int16_t>(type),
                                                           static_cast<std::int16_t>(attempts.size())}};
            });
        if (!plan) return 1;
        std::cout<<seed<<' '<<budget<<' '<<mode<<' '<<plan->remaining_budget<<' '
                 <<plan->failed_placements<<' '<<random.first()<<' '<<random.second()<<' '<<attempts.size();
        for (std::size_t i=0;i<attempts.size();++i) std::cout<<' '<<attempts[i]<<' '<<results[i];
        for (auto word:random.table()) std::cout<<' '<<word;
        const auto final_draw=session::finish_starting_unit_generation(random);
        std::cout<<' '<<final_draw<<' '<<random.first()<<' '<<random.second();
        for (auto word:random.table()) std::cout<<' '<<word;
        std::cout<<'\n';
    }
}
