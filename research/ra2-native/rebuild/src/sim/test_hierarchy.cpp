#include "hierarchy.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct LayerGolden { std::uint32_t hash; unsigned nodes,links; };
struct Golden { unsigned seed; std::array<LayerGolden,3> layers; };
const Golden goldens[]={
#include "hierarchy_golden.inc"
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
std::uint32_t hash(const HierarchyLayer& layer) {
    std::uint32_t h=2166136261u;
    const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
    for (const auto label:layer.labels) add(label);
    add(layer.nodes.size());
    for (const auto& node:layer.nodes) {
        add(node.parent); add(node.category);
        if (node.center_index) add(*node.center_index);
        add(node.links.size());
        for (const auto& link:node.links) { add(link.node); add(link.diagonal); }
    }
    return h;
}
std::vector<HierarchyCell> hierarchy_inputs(unsigned seed,unsigned& n) {
    const auto coarse=inputs(seed,n);
    std::vector<HierarchyCell> result;
    for (std::size_t i=0;i<coarse.size();++i) result.push_back({coarse[i],*coarse[i].category!=7 && (seed%5!=0 || i%7!=0)});
    return result;
}
}
int main() try {
    unsigned checks=0;
    for (const auto& golden:goldens) {
        unsigned n; const auto cells=hierarchy_inputs(golden.seed,n);
        const auto result=build_original_hierarchy(n,cells);
        for (unsigned i=0;i<3;++i) {
            const auto& layer=result.layers[2-i]; const auto& expected=golden.layers[i];
            unsigned links=0; for (const auto& node:layer.nodes) links+=node.links.size();
            if (hash(layer)!=expected.hash || layer.nodes.size()!=expected.nodes || links!=expected.links) {
                std::cerr<<"seed="<<golden.seed<<" level="<<2-i<<" hash="<<std::hex<<hash(layer)<<" expected="<<expected.hash<<std::dec<<" nodes="<<layer.nodes.size()<<" expected="<<expected.nodes<<" links="<<links<<" expected="<<expected.links<<"\n";
                throw std::runtime_error("original hierarchy layer differs");
            }
            ++checks;
        }
    }
    unsigned n; auto cells=hierarchy_inputs(0,n);
    for (auto& cell:cells) cell.usable.reset();
    try { build_original_hierarchy(n,cells); }
    catch (const std::invalid_argument&) { ++checks; }
    if (checks!=769) throw std::runtime_error("unresolved usable-area state accepted");
    std::cout<<"original hierarchy layers and ordered links: "<<checks<<" checks passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
