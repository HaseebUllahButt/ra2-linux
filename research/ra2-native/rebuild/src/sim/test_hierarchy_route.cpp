#include "hierarchy_route.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t base,yuri; };
const Golden goldens[]={
#include "hierarchy_route_golden.inc"
};
std::vector<CoarseRegionCell> inputs(unsigned seed,unsigned& n) {
    n=9+2*(seed%3); const unsigned w=(n-1)/2,h=n-1-w;
    std::uint32_t state=0x9e3779b9u^seed;
    std::vector<CoarseRegionCell> cells;
    for (unsigned y=0;y<n;++y) for (unsigned x=0;x<n;++x) {
        state=state*1664525u+1013904223u; unsigned category=(state>>24)&7;
        state=state*1664525u+1013904223u; unsigned height=(state>>16)&7;
        const bool inside=x+y>w && static_cast<int>(x)-static_cast<int>(y)<static_cast<int>(w) && static_cast<int>(y)-static_cast<int>(x)<static_cast<int>(w) && x+y<=w+2*h;
        if (seed==0) { category=0; height=0; }
        else if (seed==1) { category=x%4; height=0; }
        else if (seed==2) { category=(x+y)%2; height=0; }
        else if (seed==3) { category=0; height=(x+2*y)%8; }
        else if (seed==4) { category=6; height=(x*3+y*5)&255; }
        else if (seed==5) { category=7; height=0; }
        if (!inside) { category=7; height=0; }
        cells.push_back({static_cast<std::uint8_t>(category),static_cast<std::uint8_t>(height)});
    }
    return cells;
}
std::vector<HierarchyCell> hierarchy_inputs(unsigned seed,unsigned& n) {
    const auto coarse=inputs(seed,n);
    std::vector<HierarchyCell> result;
    for (std::size_t i=0;i<coarse.size();++i) result.push_back({coarse[i],*coarse[i].category!=7 && (seed%5!=0 || i%7!=0)});
    return result;
}std::uint32_t hash(const HierarchyRoute& result) {
    std::uint32_t h=2166136261u;
    const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
    add(result.found);
    for (int level=2;level>=0;--level) {
        const auto& layer=result.layers[level];
        add(layer.path.size()); for (const auto node:layer.path) add(node);
        for (const auto allowed:layer.allowed) add(allowed);
        for (const auto& cost:layer.best_costs) {
            add(cost.has_value());
            if (cost) { std::uint32_t bits; const float value=*cost; std::memcpy(&bits,&value,4); add(bits); }
        }
    }
    return h;
}
}
int main() try {
    unsigned checks=0;
    for (const auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) for (const auto& golden:goldens) {
        unsigned n; const auto cells=hierarchy_inputs(golden.seed,n);
        const auto graph=build_original_hierarchy(n,cells);
        std::vector<std::size_t> available;
        for (std::size_t i=0;i<cells.size();++i) if (*cells[i].coarse.category!=7) available.push_back(i);
        if (available.empty()) available.push_back(0);
        const auto first=available.front(),last=available.back(),middle=available[available.size()/2];
        const std::pair<std::size_t,std::size_t> endpoints[]={{first,last},{last,first},{middle,middle},{first,middle}};
        HierarchyRouteInputs input; input.edition=edition;
        for (int level=2;level>=0;--level) if (golden.seed%7==0 && graph.layers[level].nodes.size()>1 && !graph.layers[level].nodes[1].links.empty())
            input.blocked_edges[level].push_back({1,graph.layers[level].nodes[1].links.front().node});
        std::uint32_t h=2166136261u;
        for (unsigned zone=0;zone<(edition==DriveEdition::BaseRa2?12u:13u);++zone) for (const auto& pair:endpoints) {
            input.movement_zone=zone; input.source_index=pair.first; input.destination_index=pair.second;
            const auto result=find_original_hierarchy_route(graph,input);
            h=(h^hash(result))*16777619u; ++checks;
        }
        const auto expected=edition==DriveEdition::BaseRa2?golden.base:golden.yuri;
        if (h!=expected) {
            std::cerr<<"seed="<<golden.seed<<" hash="<<std::hex<<h<<" expected="<<expected<<"\n";
            throw std::runtime_error("original hierarchy route or search state differs");
        }
    }
    unsigned n; const auto cells=hierarchy_inputs(0,n); auto graph=build_original_hierarchy(n,cells);
    const auto rejected=[&](const Hierarchy& value,const HierarchyRouteInputs& input) {
        try { find_original_hierarchy_route(value,input); }
        catch (const std::invalid_argument&) { ++checks; return; }
        throw std::runtime_error("invalid hierarchy route state accepted");
    };
    HierarchyRouteInputs input; input.movement_zone=13; rejected(graph,input);
    input.movement_zone=0; input.source_index=cells.size(); rejected(graph,input);
    input.source_index=0; graph.layers[0].labels[0]=65535; rejected(graph,input);
    graph=build_original_hierarchy(n,cells); graph.layers[0].nodes[0].category=8; rejected(graph,input);
    graph=build_original_hierarchy(n,cells); graph.layers[0].nodes[0].links.push_back({65535,false}); rejected(graph,input);
    graph=build_original_hierarchy(n,cells); graph.layers[0].nodes[0].parent=65535; rejected(graph,input);
    std::cout<<"original hierarchy routes, costs, and allowed regions: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
