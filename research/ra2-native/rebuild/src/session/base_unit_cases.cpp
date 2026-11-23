#include "session.h"
#include <cstdint>
#include <iostream>
#include <vector>
using namespace ra2;
int main() {
    for (unsigned country=0;country<10;++country) for (std::uint32_t scenario=0;scenario<32;++scenario) {
        std::vector<session::BaseUnitCandidate> candidates;
        for (unsigned i=0;i<5;++i) {
            const std::uint32_t mask=(scenario&(1u<<i)) ? (1u<<country) : 0;
            candidates.push_back({"MCV"+std::to_string(i),mask});
        }
        const auto selected=session::select_base_unit(candidates,country);
        std::cout<<country<<' '<<scenario<<' '<<(selected ? static_cast<int>(*selected) : -1);
        for (const auto& candidate:candidates) std::cout<<' '<<*candidate.owner_mask;
        std::cout<<'\n';
    }
}
