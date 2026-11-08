#include "session.h"
#include "../sim/distance.h"
#include "../sim/sim.h"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace ra2;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    const auto distance=sim::OriginalDistance::from_executable(argv[1]);
    for (int count=1;count<=8;++count) for (unsigned mask=0;mask<(1u<<count)-1;++mask) {
        const unsigned seed=0x12345678u+count*19u+mask*997u;
        std::vector<session::StartPoint> points;
        std::vector<bool> used;
        for (int i=0;i<count;++i) {
            points.push_back({static_cast<std::int16_t>((i*37+count*11)%211),
                              static_cast<std::int16_t>((i*i*17+mask*3)%199)});
            used.push_back((mask&(1u<<i))!=0);
        }
        sim::Random random(seed);
        const auto picked=session::choose_start_point(points,used,random,distance);
        if (!picked) return 3;
        std::uint32_t hash=2166136261u;
        for (auto word:random.table()) hash=(hash^word)*16777619u;
        std::cout<<seed<<' '<<count<<' '<<mask<<' '<<*picked<<' '<<random.first()<<' '
                 <<random.second()<<' '<<hash;
        for (const auto p:points) std::cout<<' '<<p.x<<' '<<p.y;
        std::cout<<'\n';
    }
}
