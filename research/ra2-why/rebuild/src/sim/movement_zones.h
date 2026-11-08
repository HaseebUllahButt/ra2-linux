#pragma once
#include "drive.h"
#include <stdexcept>
namespace ra2::sim {
// INI readers: base 0046A300 accepts twelve values; expansion 00474E40 accepts thirteen.
inline unsigned original_movement_zone_count(DriveEdition edition) {
    if (edition==DriveEdition::BaseRa2) return 12;
    if (edition==DriveEdition::YurisRevenge) return 13;
    throw std::invalid_argument("original movement-zone edition is invalid");
}
// Base 007E00C4 / expansion 0082A594. The first twelve rows match.
inline unsigned original_movement_compatibility(DriveEdition edition,unsigned zone,unsigned category) {
    constexpr unsigned table[][8]={
        {1,2,2,2,2,2,2,3},{1,1,2,2,2,2,2,3},{1,1,1,2,2,2,2,3},
        {1,1,1,1,1,1,2,3},{1,1,2,1,1,2,2,3},{1,2,2,1,1,2,2,3},
        {1,1,1,2,2,2,1,3},{1,2,2,2,2,1,2,3},{1,1,1,2,2,1,2,3},
        {1,1,1,1,1,1,1,3},{2,2,2,2,1,2,2,3},{2,2,2,1,1,2,2,3},
        {1,1,1,2,2,2,2,3}
    };
    if (zone>=original_movement_zone_count(edition) || category>7) throw std::invalid_argument("original movement-zone or coarse category is invalid");
    return table[zone][category];
}
}
