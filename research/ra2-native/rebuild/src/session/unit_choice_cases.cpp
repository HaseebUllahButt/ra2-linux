#include "session.h"
#include "../sim/sim.h"
#include <iostream>
using namespace ra2;
int main() {
    for (int initial : {1,2,3,100,301,1000})
        for (int remaining : {1,2,33,34,100,300,301,1000})
            for (int vehicles : {0,1,2,5}) for (int infantry : {0,1,3,6}) {
                if (remaining>initial) continue;
                const auto seed=static_cast<unsigned>(initial*100+remaining+vehicles*7+infantry);
                sim::Random random(seed);
                const auto result=session::choose_starting_unit(initial,remaining,vehicles,infantry,random);
                if (!result) continue;
                std::cout<<seed<<' '<<initial<<' '<<remaining<<' '<<vehicles<<' '<<infantry<<' '
                         <<(result->group==session::StartingUnitGroup::Vehicle ? 1 : 0)<<' '
                         <<result->index<<' '<<random.first()<<' '<<random.second();
                for (auto word:random.table()) std::cout<<' '<<word;
                std::cout<<'\n';
            }
}
