#include "search_memory.h"
#include "hierarchy_route.h"
#include <iostream>
#include <iomanip>
#include <stdexcept>
using namespace ra2::sim;
std::vector<HierarchyCell> cells(unsigned seed,unsigned& n) {
    n=9+2*(seed%3); const unsigned w=(n-1)/2,h=n-1-w;
    std::uint32_t state=0x9e3779b9u^seed;
    std::vector<HierarchyCell> result;
    for (unsigned y=0;y<n;++y) for (unsigned x=0;x<n;++x) {
        state=state*1664525u+1013904223u; unsigned category=(state>>24)&7;
        state=state*1664525u+1013904223u; unsigned height=(state>>16)&7;
        const bool inside=x+y>w && int(x)-int(y)<int(w) && int(y)-int(x)<int(w) && x+y<=w+2*h;
        if (seed==0) { category=0; height=0; }
        else if (seed==1) { category=x%4; height=0; }
        else if (seed==2) { category=(x+y)%2; height=0; }
        else if (seed==3) { category=0; height=(x+2*y)%8; }
        else if (seed==4) { category=6; height=(x*3+y*5)&255; }
        else if (seed==5) { category=7; height=0; }
        if (!inside) { category=7; height=0; }
        const auto at=result.size();
        result.push_back({{static_cast<std::uint8_t>(category),static_cast<std::uint8_t>(height)},category!=7 && (seed%5!=0 || at%7!=0)});
    }
    return result;
}
int main(int argc,char**) try {
    unsigned checks=0,failed_lower=0;
    for (auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) {
        std::uint32_t total=2166136261u;
        for (unsigned seed=0;seed<32;++seed) {
            unsigned n; const auto input=cells(seed,n); const auto graph=build_original_hierarchy(n,input);
            std::vector<std::size_t> available;
            for (std::size_t i=0;i<input.size();++i) if (*input[i].coarse.category!=7) available.push_back(i);
            if (available.empty()) available.push_back(0);
            const auto first=available.front(),last=available.back(),middle=available[available.size()/2];
            const std::pair<std::size_t,std::size_t> endpoints[]={{first,last},{last,first},{middle,middle},{first,middle}};
            std::uint32_t hash=2166136261u;
            for (unsigned zone:{0u,2u,9u}) for (const auto& pair:endpoints) {
                HierarchyRouteInputs route_input; route_input.edition=edition; route_input.movement_zone=zone;
                route_input.source_index=pair.first; route_input.destination_index=pair.second;
                for (unsigned level=0;level<3;++level) if (seed%7==0 && graph.layers[level].nodes.size()>1 && !graph.layers[level].nodes[1].links.empty())
                    route_input.blocked_edges[level].push_back({1,graph.layers[level].nodes[1].links.front().node});
                const auto route=find_original_hierarchy_route(graph,route_input);
                OriginalSearchMemory memory;
                for (unsigned i=0;i<1500;++i) memory.paths[i/500][i%500]=i+33;
                memory.counts={8,9,10}; memory.progress_6c=17; memory.progress_cell_70={123,456};
                retain_original_hierarchy_paths(memory,route);
                unsigned mask=0;
                for (unsigned level=0;level<3;++level) if (!route.layers[level].path.empty()) mask|=1u<<level;
                if (!route.found && (mask&4) && mask!=7) ++failed_lower;
                const auto mix=[&](std::uint32_t value) { hash=(hash^value)*16777619u; };
                mix(route.found); mix(mask); mix(memory.progress_6c);
                mix(memory.progress_cell_70.x|(static_cast<std::uint32_t>(memory.progress_cell_70.y)<<16));
                for (auto count:memory.counts) mix(count);
                for (const auto& row:memory.paths) for (auto word:row) mix(word);
                ++checks;
            }
            total=(total^hash)*16777619u;
            if (argc>1) std::cout<<(edition==DriveEdition::BaseRa2?1:2)<<" "<<seed<<" "<<std::hex<<std::setw(8)<<std::setfill('0')<<hash<<std::dec<<"\n";
        }
        if (argc==1) {
            std::cout<<"hierarchy retained-write digest "<<std::hex<<total<<std::dec<<"\n";
            if (total!=0x5d485d4eu) throw std::runtime_error("original hierarchy retained writes differ");
        }
    }
    if (!failed_lower) throw std::runtime_error("hierarchy retention fixture missed failed lower levels");
    if (argc==1) std::cout<<checks<<" original hierarchy memory cases pass; failed lower levels="<<failed_lower<<"\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
