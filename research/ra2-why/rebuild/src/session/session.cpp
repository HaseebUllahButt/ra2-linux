#include "session.h"
#include "../sim/distance.h"
#include "../sim/sim.h"
#include <algorithm>
#include <limits>

namespace ra2::session {
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
