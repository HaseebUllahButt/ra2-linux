#include "replacement.h"
#include <limits>
#include <stdexcept>
namespace ra2::sim {
namespace {
constexpr const char* names[]={"RampBase","RampSmooth","RoughTile","SandTile","GreenTile","PaveTile",
    "ClearToRoughLat","ClearToSandLat","ClearToGreenLat","ClearToPaveLat","MiscPaveTile","ShorePieces","PavedRoads","Medians","WaterBridge"};
bool in_range(int value,int first,int extent) {
    const auto last=first==-1?-1:static_cast<std::int64_t>(first)+extent;
    return value>=first && value<=last;
}
}
TileReplacementRules TileReplacementRules::from_theater(const TheaterInputs& input) {
    TileReplacementRules result;
    for (unsigned i=0;i<result.first.size();++i) {
        const auto set=input.settings.read_integer("General",names[i],-1);
        if (set<0 || static_cast<std::size_t>(set)>=input.tile_sets.size()) continue;
        const auto& range=input.tile_sets[set];
        if (range.first>65535 || range.count>65535 || range.first+range.count>65536)
            throw std::invalid_argument("original theater tile range exceeds the Cell index domain");
        result.first[i]=static_cast<int>(range.first);
    }
    // 00545150 disables these two live groups in theater index five (Lunar).
    if (input.theater_name=="LUNAR") {
        result.first[static_cast<unsigned>(TileRole::ShorePieces)]=-1;
        result.first[static_cast<unsigned>(TileRole::WaterBridge)]=-1;
    }
    return result;
}
TileReplacementDecision original_tile_replacement(const TileReplacementInput& in,const TileReplacementRules& rules) {
    if (in.index>65535) return {{},"Cell +38 index is outside the recovered live 16-bit map domain"};
    int index=static_cast<int>(in.index);
    auto first=[&](TileRole role){return rules.at(role);};
    const TileRole bases[]={TileRole::RoughTile,TileRole::SandTile,TileRole::GreenTile,TileRole::PaveTile};
    const TileRole borders[]={TileRole::ClearToRoughLat,TileRole::ClearToSandLat,TileRole::ClearToGreenLat,TileRole::ClearToPaveLat};
    for (unsigned group=0;group<4;++group) {
        const int base=first(bases[group]), border=first(borders[group]);
        // The first original branch has no explicit border != -1 guard.
        if (group && border==-1) continue;
        if (index!=base && !in_range(index,border,15)) continue;
        unsigned mask=0;
        for (unsigned d=0;d<4;++d) {
            if (!in.neighbors[d]) return {{},"0047CA80 border decision needs a missing cardinal neighbor index"};
            if (in.neighbors[d]->index>65535) return {{},"0047CA80 neighbor index is outside the recovered Cell domain"};
            const int neighbor=static_cast<int>(in.neighbors[d]->index);
            bool connected=neighbor==base || in_range(neighbor,border,15);
            if (group==2) connected=connected || in_range(neighbor,first(TileRole::ShorePieces),41) || in_range(neighbor,first(TileRole::WaterBridge),1);
            if (group==3) connected=connected || in_range(neighbor,first(TileRole::MiscPaveTile),13) || in_range(neighbor,first(TileRole::Medians),13) || in_range(neighbor,first(TileRole::PavedRoads),20);
            if (!connected) mask|=1u<<d;
        }
        index=mask?border+static_cast<int>(mask):base;
    }
    const int ramp_base=first(TileRole::RampBase),smooth=first(TileRole::RampSmooth);
    if (in_range(index,ramp_base,19) || in_range(index,smooth,11)) {
        // 0047CA80 selects the pair in this order for each of the four basic ramps.
        constexpr unsigned pairs[][2]={{3,1},{0,2},{1,3},{2,0}};
        unsigned mask=0;
        if (in.ramp>=1 && in.ramp<=4) {
            for (unsigned bit=0;bit<2;++bit) {
                const auto& neighbor=in.neighbors[pairs[in.ramp-1][bit]];
                if (!neighbor) return {{},"0047CA80 ramp smoothing needs a missing cardinal neighbor ramp"};
                if (!neighbor->ramp) mask|=1u<<bit;
            }
            if (mask) index=smooth+static_cast<int>((in.ramp-1)*3+mask-1);
        }
        if (!mask) index=ramp_base+static_cast<int>(in.ramp)-1;
    }
    if (index<0 || index>65535) return {{},"0047CA80 produces an invalid tile index with these unresolved theater groups"};
    return {static_cast<std::uint32_t>(index),{}};
}
}
