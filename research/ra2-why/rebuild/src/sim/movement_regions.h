#pragma once
#include "regions.h"
#include "movement_zones.h"
namespace ra2::sim {
struct MovementRegions {
    DriveEdition edition=DriveEdition::BaseRa2;
    std::vector<std::vector<std::uint16_t>> neighbors;
    std::vector<std::vector<std::uint16_t>> components; // One region-label table per valid movement zone.
};
// Final graph/table pass in base 0054EBD0 / expansion 0056C510.
// The caller supplies original ordered region pairs, including any resolved transition edges.
MovementRegions build_original_movement_regions(const CoarseRegions&,DriveEdition);
// Ground-only tail of base 0054F8F0 / expansion 0056D230, with bridge selection false.
// Preserves signed coordinates, linear aliases, and clamping before the raw-region lookup.
std::uint16_t original_ground_component(const CoarseRegions&,const MovementRegions&,unsigned stride,
    unsigned movement_zone,std::int16_t x,std::int16_t y);
}
