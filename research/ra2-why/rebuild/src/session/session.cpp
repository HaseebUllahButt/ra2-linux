#include "session.h"
#include "../sim/distance.h"
#include "../sim/sim.h"
#include <algorithm>
#include <limits>

namespace ra2::session {
HouseValues initialize_house_values(std::int32_t credits, std::int32_t color) noexcept {
    // Base 004E8300; expansion 004FCE00. Both multiplayer human and AI
    // constructors call this before later map and starting-unit processing.
    return {credits,credits,color,color};
}
std::optional<std::size_t> select_base_unit(const std::vector<BaseUnitCandidate>& candidates,
                                            unsigned country_index) {
    // 005D7030 passes RulesClass BaseUnit to 00505310. The latter scans
    // candidates in order and tests the resolved country ownership bit.
    if (country_index>=32) return std::nullopt;
    const std::uint32_t bit=std::uint32_t{1}<<country_index;
    for (std::size_t i=0;i<candidates.size();++i) {
        if (!candidates[i].owner_mask || candidates[i].type.empty()) return std::nullopt;
        if (*candidates[i].owner_mask & bit) return i;
    }
    return std::nullopt;
}
namespace {
std::optional<int> point_distance(StartPoint a, StartPoint b, const sim::OriginalDistance& distance) {
    // Base 005B7920 and expansion 005D6890 narrow the differences and result to signed 16-bit values.
    // Reject overflow instead of reproducing invalid point data.
    const int dx=int(a.x)-int(b.x), dy=int(a.y)-int(b.y);
    if (dx<std::numeric_limits<std::int16_t>::min() || dx>std::numeric_limits<std::int16_t>::max() ||
        dy<std::numeric_limits<std::int16_t>::min() || dy>std::numeric_limits<std::int16_t>::max())
        return std::nullopt;
    const auto squared=double(dx)*dx+double(dy)*dy;
    const auto value=distance.square_root(squared);
    if (value>=32768.0f) return std::nullopt;
    // 007C5F00 sets the x87 control word to 0E7F, which truncates.
    return static_cast<int>(value);
}
}
std::optional<StartPoint> find_initial_placement(StartPoint origin, PlacementBounds bounds,
    PlacementEdition edition, sim::Random& random, const PlacementCheck& valid, const PlacementCheck& place) {
    // Base 00660B60; expansion 00688ED0. Callers resolve both operations.
    if (!valid || !place || bounds.left>bounds.right || bounds.top>bounds.bottom) return std::nullopt;
    if (valid(origin,random) && place(origin,random)) return origin;
    static constexpr int dx[8]={0,1,1,1,0,-1,-1,-1};
    static constexpr int dy[8]={-1,-1,0,1,1,1,0,-1};
    for (int radius=1;radius<32;++radius) {
        int direction=random.ranged(0,7);
        for (int pass=0;pass<2;++pass) for (int i=0;i<8;++i) {
            int x=std::clamp(int(origin.x)+dx[direction]*radius,int(bounds.left),int(bounds.right));
            int y=std::clamp(int(origin.y)+dy[direction]*radius,int(bounds.top),int(bounds.bottom));
            if (pass) {
                const int x_step=random.ranged(0,1);
                const int x_sign=random.ranged(0,99);
                x=std::clamp(x+(x_sign<50 ? x_step : -x_step),int(bounds.left),int(bounds.right));
                const int y_step=random.ranged(0,1);
                const int y_sign=random.ranged(0,99);
                y=std::clamp(y+(y_sign<50 ? y_step : -y_step),int(bounds.top),int(bounds.bottom));
            }
            const StartPoint point{static_cast<std::int16_t>(x),static_cast<std::int16_t>(y)};
            const bool cell_valid=edition==PlacementEdition::Base || valid(point,random);
            if (cell_valid && (point.x!=origin.x || point.y!=origin.y) && place(point,random)) return point;
            direction=(direction+1)&7;
        }
    }
    return std::nullopt;
}
std::optional<std::size_t> choose_start_point(const std::vector<StartPoint>& points,
    std::vector<bool>& used, sim::Random& random,
    const sim::OriginalDistance& distance) {
    // 005D6C70 supplies sixteen occupied flags to Battle mode 005D6890.
    if (points.empty() || points.size()>16 || used.size()!=points.size()) return std::nullopt;
    const auto occupied=static_cast<std::size_t>(std::count(used.begin(),used.end(),true));
    if (occupied==points.size()) return std::nullopt;
    std::vector<std::size_t> free;
    for (std::size_t i=0;i<points.size();++i) if (!used[i]) free.push_back(i);
    std::size_t selected=free.front();
    // Base 005B79A8 and expansion 005D6890 pick a random first point. Later regular players get the
    // point with the greatest sum of distances to occupied points.
    if (occupied==0) {
        selected=free[static_cast<std::size_t>(random.ranged(0,static_cast<int>(free.size()-1)))];
    } else {
        int best=-1;
        for (const auto candidate : free) {
            int sum=0;
            for (std::size_t i=0;i<points.size();++i) if (used[i]) {
                const auto d=point_distance(points[candidate],points[i],distance);
                if (!d) return std::nullopt;
                sum+=*d;
            }
            // Strict greater-than retains the first equal-scoring point.
            if (sum>best) {selected=candidate;best=sum;}
        }
    }
    used[selected]=true;
    return selected;
}
}
