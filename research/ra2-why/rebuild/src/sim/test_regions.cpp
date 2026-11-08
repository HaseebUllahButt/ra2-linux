#include "regions.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t hash; unsigned groups,edges; };
const Golden goldens[]={
#include "region_golden.inc"
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
std::uint32_t hash(const CoarseRegions& result) {
    std::uint32_t h=2166136261u;
    const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
    for (const auto label:result.labels) add(label);
    add(result.categories.size());
    for (const auto category:result.categories) add(category);
    for (const auto count:result.reported_sizes) add(static_cast<std::uint32_t>(count));
    add(result.largest); add(result.edges.size());
    for (const auto& edge:result.edges) add((static_cast<std::uint32_t>(edge.first)<<16)|edge.second);
    return h;
}
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() try {
    unsigned checks=0;
    for (const auto& golden:goldens) {
        unsigned n; const auto cells=inputs(golden.seed,n);
        const auto result=build_original_coarse_regions(n,cells);
        if (hash(result)!=golden.hash || result.categories.size()!=golden.groups || result.edges.size()!=golden.edges) {
            std::cerr<<"seed="<<golden.seed<<" hash="<<std::hex<<hash(result)<<" expected="<<golden.hash<<std::dec<<" groups="<<result.categories.size()<<" expected="<<golden.groups<<" edges="<<result.edges.size()<<" expected="<<golden.edges<<"\n";
            throw std::runtime_error("original coarse regions differ");
        }
        ++checks;
    }
    const auto rejected=[&](unsigned stride,const std::vector<CoarseRegionCell>& cells) {
        try { build_original_coarse_regions(stride,cells); }
        catch (const std::invalid_argument&) { ++checks; return; }
        throw std::runtime_error("unresolved coarse region input accepted");
    };
    unsigned n; auto cells=inputs(0,n);
    rejected(0,cells); rejected(n,{});
    cells[0].category=0; rejected(n,cells); cells[0].category=7;
    cells[n+1].category.reset(); rejected(n,cells); cells[n+1].category=8; rejected(n,cells);
    cells=inputs(0,n); cells[5*n+5].height.reset(); rejected(n,cells);
    cells=inputs(5,n); for (auto& cell:cells) cell.height.reset();
    require(build_original_coarse_regions(n,cells).largest==0xffffffffu,"empty original grid has a largest region"); ++checks;
    std::cout<<"original coarse regions and explicit input limits: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
