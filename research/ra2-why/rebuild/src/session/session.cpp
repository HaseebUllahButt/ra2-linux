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
std::optional<std::int32_t> starting_unit_budget(std::int32_t unit_count,
    std::int32_t tech_level, std::uint32_t house_mask,
    const std::vector<StartingUnitType>& types) {
    // 005D6D80 counts eligible types across the active houses. The engine
    // excludes BaseUnit vehicles and uses each type's resolved Cost call.
    if (unit_count<=0) return 0;
    std::int64_t sum=0, count=0;
    for (const auto& type : types) {
        if (!type.allowed) return std::nullopt;
        if (!*type.allowed) continue;
        if (!type.owner_mask) return std::nullopt;
        if (!(*type.owner_mask & house_mask)) continue;
        if (!type.tech_level) return std::nullopt;
        if (*type.tech_level>tech_level) continue;
        if (type.vehicle) {
            if (!type.base_unit) return std::nullopt;
            if (*type.base_unit) continue;
        }
        if (!type.cost || *type.cost<0) return std::nullopt;
        sum+=*type.cost;
        ++count;
        if (sum>std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    }
    if (!count) count=1;
    // 005D6EB8 rounds the average before multiplying by UnitCount.
    const auto rounded=(sum+count/2)/count;
    const auto budget=rounded*unit_count;
    if (sum+count/2>std::numeric_limits<std::int32_t>::max() ||
        budget>std::numeric_limits<std::int32_t>::max()) return std::nullopt;
    return static_cast<std::int32_t>(budget);
}

std::optional<StartingUnitChoice> choose_starting_unit(std::int32_t initial_budget,
    std::int32_t remaining_budget, std::size_t vehicle_count, std::size_t infantry_count,
    sim::Random& random) {
    // Base 005B8282; expansion 005D732F. Lists are the mode's already
    // filtered candidates. The last third of the budget selects infantry.
    if (initial_budget<=0 || remaining_budget<=0 ||
        vehicle_count>std::size_t(std::numeric_limits<std::int32_t>::max()) ||
        infantry_count>std::size_t(std::numeric_limits<std::int32_t>::max())) return std::nullopt;
    const bool vehicle=remaining_budget>initial_budget/3 && vehicle_count>0;
    const auto count=vehicle ? vehicle_count : infantry_count;
    // The original indexes an empty list here. Reject it safely.
    if (!count) return std::nullopt;
    const auto index=static_cast<std::size_t>(random.ranged(0,static_cast<std::int32_t>(count-1)));
    return StartingUnitChoice{vehicle ? StartingUnitGroup::Vehicle : StartingUnitGroup::Infantry,index};
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
    return find_initial_placement(origin,bounds,1,edition,random,valid,place);
}
std::optional<StartPoint> find_initial_placement(StartPoint origin, PlacementBounds bounds,
    std::int32_t start_radius, PlacementEdition edition, sim::Random& random,
    const PlacementCheck& valid, const PlacementCheck& place) {
    // Base 00660B60; expansion 00688ED0. Callers resolve both operations.
    if (!valid || !place || start_radius<0 || bounds.left>bounds.right || bounds.top>bounds.bottom)
        return std::nullopt;
    if (valid(origin,random) && place(origin,random)) return origin;
    static constexpr int dx[8]={0,1,1,1,0,-1,-1,-1};
    static constexpr int dy[8]={-1,-1,0,1,1,1,0,-1};
    for (int radius=start_radius;radius<32;++radius) {
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
