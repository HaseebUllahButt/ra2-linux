#pragma once
#include "world.h"
#include <array>
namespace ra2::sim {
enum class TileRole : unsigned {
    RampBase,RampSmooth,RoughTile,SandTile,GreenTile,PaveTile,
    ClearToRoughLat,ClearToSandLat,ClearToGreenLat,ClearToPaveLat,
    MiscPaveTile,ShorePieces,PavedRoads,Medians,WaterBridge,Count
};
struct TileReplacementRules {
    std::array<int,static_cast<unsigned>(TileRole::Count)> first;
    TileReplacementRules() { first.fill(-1); }
    static TileReplacementRules from_theater(const TheaterInputs&);
    int at(TileRole role) const { return first.at(static_cast<unsigned>(role)); }
};
struct TileNeighbor { std::uint32_t index; std::uint8_t ramp; };
struct TileReplacementInput {
    std::uint32_t index=0xffff;
    std::uint8_t ramp=0;
    // Original cardinal order: north, east, south, west (0049F2F0).
    std::array<std::optional<TileNeighbor>,4> neighbors;
};
struct TileReplacementDecision { std::optional<std::uint32_t> index; std::string detail; };
// Full index decision at base 00471D10 / expansion 0047CA80.
// Static metadata loading after a changed index remains the caller's separate step.
TileReplacementDecision original_tile_replacement(const TileReplacementInput&,const TileReplacementRules&);
}
