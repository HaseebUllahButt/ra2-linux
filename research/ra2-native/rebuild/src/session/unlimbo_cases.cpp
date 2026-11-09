#include "session.h"
#include "../sim/sim.h"
#include <iostream>
using namespace ra2;
int main() {
    for (unsigned seed : {0u,1u,123u,4294967295u})
        for (int facing : {0,1,64,127,255})
            for (int flags=0;flags<16;++flags) {
                sim::Random random(seed);
                const auto result=session::unit_unlimbo_suffix(static_cast<std::uint8_t>(facing),
                    flags&1,flags&2,flags&4,flags&8,random);
                std::cout<<seed<<' '<<facing<<' '<<flags<<' '<<result.facing_value<<' '
                    <<result.status.value_or(77)<<' '<<result.timer_length<<' '<<int(result.timer_active)<<' '
                    <<random.first()<<' '<<random.second();
                for (const auto value:random.table()) std::cout<<' '<<value;
                std::cout<<'\n';
            }
}
