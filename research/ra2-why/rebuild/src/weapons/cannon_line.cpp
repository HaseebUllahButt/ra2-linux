#include "cannon_line.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace ra2::weapons {
TerrainTraceResult base_cannon_line_trace(const CannonLineInput& input,const CannonLineLookup& lookup) {
    using S=TerrainTraceStatus;
    auto missing=[] {return TerrainTraceResult{S::NeedsCell,0};};
    if (!input.checks_cliffs && !input.checks_walls) return {S::Clear,0};
    if (!lookup) return missing();
    const auto coord=[](int n) {const auto bits=static_cast<std::uint32_t>(n/256)&0xffffu;
        return static_cast<std::int16_t>(bits<0x8000?static_cast<int>(bits):static_cast<int>(bits)-0x10000);};
    auto source=lookup(coord(input.origin.x),coord(input.origin.y));
    auto target=lookup(coord(input.target.x),coord(input.target.y));
    if (!source || !target || !source->terrain.id || !target->terrain.id) return missing();
    if (source->level < -128 || source->level > 127 || target->level < -128 || target->level > 127)
        return missing();
    const int dx=std::abs(int(coord(input.origin.x))-int(coord(input.target.x)));
    const int dy=std::abs(int(coord(input.origin.y))-int(coord(input.target.y)));
    const int steps=std::max(dx,dy);
    if (!steps) return {S::Clear,0};
    const auto difference=[](int a,int b)->std::optional<int> {
        const auto d=static_cast<long long>(a)-b;
        if (d<std::numeric_limits<int>::min() || d>std::numeric_limits<int>::max()) return {};
        return static_cast<int>(d);
    };
    const auto x=difference(input.target.x,input.origin.x),y=difference(input.target.y,input.origin.y),
               z=difference(input.target.z,input.origin.z);
    if (!x || !y || !z) return missing();
    const int sx=*x/steps,sy=*y/steps,sz=*z/steps;
    auto previous=*source;
    for (int i=0;i<steps;++i) {
        const auto px=static_cast<long long>(input.origin.x)+static_cast<long long>(sx)*i;
        const auto py=static_cast<long long>(input.origin.y)+static_cast<long long>(sy)*i;
        const auto pz=static_cast<long long>(input.origin.z)+static_cast<long long>(sz)*i;
        if (px<std::numeric_limits<int>::min() || px>std::numeric_limits<int>::max() ||
            py<std::numeric_limits<int>::min() || py>std::numeric_limits<int>::max() ||
            pz<std::numeric_limits<int>::min() || pz>std::numeric_limits<int>::max()) return missing();
        auto current=lookup(coord(static_cast<int>(px)),coord(static_cast<int>(py)));
        if (!current || !current->terrain.id) return missing();
        const TerrainTraceInput step{{static_cast<int>(px),static_cast<int>(py),static_cast<int>(pz)},
            target->terrain.id,previous.terrain.floor,source->terrain.floor,source->level,target->level,
            input.checks_cliffs,input.checks_walls,input.allied_wall_rule};
        TerrainTraceResult result{S::NeedsCell,0};
        try {
            result=base_trace_terrain(step,[&](std::int16_t cx,std::int16_t cy)->std::optional<TerrainTraceCell> {
                auto cell=lookup(cx,cy); if (!cell) return {}; return cell->terrain;
            });
        } catch (const std::invalid_argument&) { return missing(); }
        if (result.status==S::NeedsCell) return result;
        if (result.status==S::Hit) {
            // 004BBAA0 accepts the first hit if 00475840(-1,-1) sees
            // overlay index 2 or 26 and Warhead +144 is set.
            if (input.warhead_flag_144 && (current->overlay_index==2 || current->overlay_index==26))
                return {S::Clear,0};
            return result;
        }
        previous=*current;
    }
    return {S::Clear,0};
}
}
