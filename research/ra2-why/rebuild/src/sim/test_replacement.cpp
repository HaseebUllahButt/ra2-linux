#include "replacement.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::sim;
int main() try {
    TileReplacementRules rules;
    for (unsigned i=0;i<rules.first.size();++i) rules.first[i]=(i+1)*100;
    std::uint32_t hash=2166136261u; unsigned count=0;
    auto run=[&](const TileReplacementInput& in) {
        const auto result=original_tile_replacement(in,rules);
        if (!result.index) throw std::runtime_error(result.detail);
        hash=(hash^*result.index)*16777619u; ++count;
    };
    // Resolved disjoint catalog ranges match original_replacement_check.py.
    for (unsigned group=0;group<4;++group) {
        const unsigned center=(group+3)*100,border=(group+7)*100;
        std::vector<unsigned> choices={60,center,border+7};
        if (group==2) { choices.push_back(1241); choices.push_back(1501); }
        if (group==3) { choices.push_back(1113); choices.push_back(1320); choices.push_back(1413); }
        for (unsigned index:{center,border,border+7,border+15})
            for (auto north:choices) for (auto east:choices) for (auto south:choices) for (auto west:choices) {
                TileReplacementInput in; in.index=index;
                in.neighbors={TileNeighbor{north,0},TileNeighbor{east,0},TileNeighbor{south,0},TileNeighbor{west,0}};
                run(in);
            }
    }
    std::vector<unsigned> ramps;
    for (unsigned i=100;i<120;++i) ramps.push_back(i);
    for (unsigned i=200;i<212;++i) ramps.push_back(i);
    for (auto index:ramps) for (unsigned ramp=0;ramp<=20;++ramp) for (unsigned mask=0;mask<16;++mask) {
        TileReplacementInput in; in.index=index; in.ramp=ramp;
        for (unsigned d=0;d<4;++d) in.neighbors[d]=TileNeighbor{60,static_cast<std::uint8_t>(mask&(1u<<d)?0:1)};
        run(in);
    }
    if (count!=19084 || hash!=0x60dc1ec1u) throw std::runtime_error("original tile replacement replay differs");
    TileReplacementInput absent; absent.index=300;
    if (original_tile_replacement(absent,rules).index) throw std::runtime_error("missing required neighbor was accepted");
    absent.index=0xffff;
    if (original_tile_replacement(absent,rules).index!=0xffff) throw std::runtime_error("clear sentinel changed");
    TheaterInputs catalog; catalog.theater_name="TEMPERATE";
    catalog.settings.parse("[General]\nRampBase=1\nRampSmooth=2\nShorePieces=3\nWaterBridge=4\n");
    for (unsigned i=0;i<5;++i) catalog.tile_sets.push_back({i*100,100});
    auto parsed=TileReplacementRules::from_theater(catalog);
    if (parsed.at(TileRole::RampBase)!=100 || parsed.at(TileRole::RampSmooth)!=200 || parsed.at(TileRole::RoughTile)!=-1)
        throw std::runtime_error("original General TileSet numbers were not resolved through the catalog");
    catalog.theater_name="LUNAR"; parsed=TileReplacementRules::from_theater(catalog);
    if (parsed.at(TileRole::ShorePieces)!=-1 || parsed.at(TileRole::WaterBridge)!=-1 || parsed.at(TileRole::RampBase)!=100)
        throw std::runtime_error("original Lunar group overrides differ");
    std::cout<<"19084 native replacement cases match both original editions; hash 60dc1ec1\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<"\n"; return 1; }
