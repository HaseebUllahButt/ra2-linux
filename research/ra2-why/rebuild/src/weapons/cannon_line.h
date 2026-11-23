#pragma once
#include "weapons.h"
#include <functional>
#include <optional>
namespace ra2::weapons {
struct CannonLineCell {
    TerrainTraceCell terrain; // Original Cell identity, 0047CF10 floor and wall state.
    int level; // Signed Cell +11B, without the separate +140 bit 80.
    int overlay_index; // Original Cell +44; -1 means no overlay.
};
using CannonLineLookup=std::function<std::optional<CannonLineCell>(std::int16_t,std::int16_t)>;
struct CannonLineInput {
    sim::Coord origin,target;
    bool checks_cliffs,checks_walls,allied_wall_rule,warhead_flag_144;
};
// Base 004BBAA0 -> 004BB890 -> 004BBAF0, with a stable resolved map.
// A Hit has a nonzero original cell identity. Missing cells, including the
// out-of-map fallback, return NeedsCell. The caller owns loaded map state.
TerrainTraceResult base_cannon_line_trace(const CannonLineInput&,const CannonLineLookup&);
}
