#include "movement_regions.h"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
namespace {
struct Golden { unsigned seed; std::uint32_t base,yuri; unsigned regions,edges; };
const Golden goldens[]={
#include "movement_regions_golden.inc"
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
std::array<std::pair<int,int>,14> coordinates(int n) {
    return {{{-5,-5},{-1,0},{0,-1},{0,0},{1,1},{n-1,n-1},{n,n},{n+5,1},{-n+5,3},{-1,n},{32767,32767},{-32768,-32768},{5,5},{-1,1}}};
}
std::uint32_t hash(const CoarseRegions& graph,const MovementRegions& movement,unsigned stride,unsigned& queries) {
    std::uint32_t h=2166136261u;
    const auto add=[&](std::uint32_t value) { h=(h^value)*16777619u; };
    add(graph.categories.size()); add(graph.largest);
    for (const auto label:graph.labels) add(label);
    add(graph.edges.size());
    for (const auto& edge:graph.edges) add((static_cast<std::uint32_t>(edge.first)<<16)|edge.second);
    for (unsigned zone=0;zone<movement.components.size();++zone) {
        for (const auto component:movement.components[zone]) add(component);
        for (const auto& point:coordinates(stride)) {
            add(original_ground_component(graph,movement,stride,zone,static_cast<std::int16_t>(point.first),static_cast<std::int16_t>(point.second)));
            ++queries;
        }
    }
    return h;
}
}
int main() try {
    unsigned graphs=0,queries=0,errors=0;
    for (const auto edition:{DriveEdition::BaseRa2,DriveEdition::YurisRevenge}) for (const auto& golden:goldens) {
        unsigned n; const auto cells=inputs(golden.seed,n);
        const auto graph=build_original_coarse_regions(n,cells);
        const auto movement=build_original_movement_regions(graph,edition);
        const auto value=hash(graph,movement,n,queries);
        const auto expected=edition==DriveEdition::BaseRa2?golden.base:golden.yuri;
        if (value!=expected || graph.categories.size()!=golden.regions || graph.edges.size()!=golden.edges) {
            std::cerr<<"seed="<<golden.seed<<" hash="<<std::hex<<value<<" expected="<<expected<<"\n";
            throw std::runtime_error("original movement components or ground lookup differs");
        }
        ++graphs;
    }
    const auto rejects=[&](auto function) {
        try { function(); }
        catch (const std::invalid_argument&) { ++errors; return; }
        throw std::runtime_error("invalid movement-region state accepted");
    };
    unsigned n; const auto cells=inputs(1,n); const auto good=build_original_coarse_regions(n,cells);
    auto bad=good; bad.categories[0]=0; rejects([&] { build_original_movement_regions(bad,DriveEdition::BaseRa2); });
    bad=good; bad.categories[1]=8; rejects([&] { build_original_movement_regions(bad,DriveEdition::BaseRa2); });
    bad=good; bad.edges.push_back(bad.edges.front()); rejects([&] { build_original_movement_regions(bad,DriveEdition::BaseRa2); });
    bad=good; bad.edges={{1,1}}; rejects([&] { build_original_movement_regions(bad,DriveEdition::BaseRa2); });
    bad=good; bad.edges={{1,65535}}; rejects([&] { build_original_movement_regions(bad,DriveEdition::BaseRa2); });
    rejects([&] { build_original_movement_regions(good,static_cast<DriveEdition>(99)); });
    const auto movement=build_original_movement_regions(good,DriveEdition::BaseRa2);
    rejects([&] { original_ground_component(good,movement,n,12,0,0); });
    rejects([&] { original_ground_component(good,movement,1,0,0,0); });
    bad=good; bad.labels[0]=65535; rejects([&] { original_ground_component(bad,movement,n,0,0,0); });
    auto wrong=movement; wrong.components[0].clear(); rejects([&] { original_ground_component(good,wrong,n,0,0,0); });
    std::cout<<"original movement regions: "<<graphs<<" graph hashes, "<<queries<<" ground queries, "<<errors<<" input errors passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
