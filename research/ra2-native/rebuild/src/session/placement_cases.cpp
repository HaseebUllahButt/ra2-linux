#include "session.h"
#include "../sim/sim.h"
#include <iostream>
#include <vector>
using namespace ra2;
int main(int argc,char** argv) {
    if (argc!=2) return 2;
    const auto edition=std::string(argv[1])=="base" ? session::PlacementEdition::Base : session::PlacementEdition::Yuri;
    for (int radius : {1,4,32}) for (int wanted : {1,2,5,15,25,70,0})
        for (unsigned seed : {0u,42u,987654u}) {
        sim::Random random(seed);
        std::vector<session::StartPoint> attempts;
        const auto result=session::find_initial_placement({5,6},{0,0,13,15},radius,edition,random,
            [&](session::StartPoint point,sim::Random&) {
                attempts.push_back(point);
                return wanted && attempts.size()==static_cast<std::size_t>(wanted);
            },[&](session::StartPoint point,sim::Random&) {
                if (edition==session::PlacementEdition::Base && (point.x!=5 || point.y!=6)) {
                    attempts.push_back(point);
                    return wanted && attempts.size()==static_cast<std::size_t>(wanted);
                }
                return true;
            });
        std::cout<<seed<<' '<<wanted<<' '<<radius<<' '<<(result ? 1 : 0)<<' '<<random.first()<<' '
                 <<random.second()<<' '<<attempts.size();
        for (const auto& p:attempts) std::cout<<' '<<p.x<<' '<<p.y;
        for (auto word:random.table()) std::cout<<' '<<word;
        std::cout<<'\n';
    }
}
