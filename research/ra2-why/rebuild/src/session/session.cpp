#include "session.h"
#include "../sim/distance.h"
#include "../sim/sim.h"
#include <algorithm>
#include <limits>

namespace ra2::session {
std::int32_t multiplayer_house_color(std::int32_t selected_color) noexcept {
    // Base 006715D0 / Yuri 0069A310. Both original tables have these bytes.
    constexpr std::int32_t palette[]={3,11,21,29,13,25,17,15,5};
    if (selected_color==-2) return palette[8];
    if (selected_color>=0 && selected_color<9) return palette[selected_color];
    return selected_color;
}
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

std::optional<StartingUnitRun> generate_starting_units(std::int32_t budget,
    std::int32_t house_tech_level, std::uint32_t house_bit,
    const std::vector<StartingUnitType>& types, sim::Random& random, const UnitAttempt& attempt) {
    // 005D70F0 filters by the current house, then runs the unit budget loop.
    // Do not invoke unresolved callbacks or accept more than one house bit.
    if (budget<0 || !house_bit || (house_bit & (house_bit-1)) || !attempt) return std::nullopt;
    StartingUnitRun result{{},budget,0};
    if (!budget) return result;
    std::vector<std::size_t> vehicles, infantry;
    int minimum_cost=std::numeric_limits<int>::max();
    for (std::size_t i=0;i<types.size();++i) {
        const auto& type=types[i];
        if (!type.allowed) return std::nullopt;
        if (!*type.allowed) continue;
        if (!type.owner_mask) return std::nullopt;
        if (!(*type.owner_mask & house_bit)) continue;
        if (!type.tech_level) return std::nullopt;
        if (*type.tech_level>house_tech_level) continue;
        if (type.vehicle) {
            if (!type.base_unit) return std::nullopt;
            if (*type.base_unit) continue;
        }
        if (!type.cost || *type.cost<=0) return std::nullopt;
        minimum_cost=std::min(minimum_cost,*type.cost);
        (type.vehicle ? vehicles : infantry).push_back(i);
    }
    if ((vehicles.empty() && infantry.empty()) ||
        std::int64_t(budget)/minimum_cost+1>100000) return std::nullopt;
    auto working=random;
    while (result.remaining_budget>0 && result.failed_placements<20) {
        const auto choice=choose_starting_unit(budget,result.remaining_budget,
                                                vehicles.size(),infantry.size(),working);
        if (!choice) return std::nullopt;
        const auto index=choice->group==StartingUnitGroup::Vehicle ?
                         vehicles[choice->index] : infantry[choice->index];
        const auto placed=attempt(index,working);
        if (!placed) return std::nullopt;
        if (!placed->placed) {++result.failed_placements;continue;}
        result.placed.push_back({index,placed->cell});
        result.remaining_budget-=*types[index].cost;
    }
    random=working;
    return result;
}

StartingMission starting_unit_mission(bool human) noexcept {
    // Base 005B838E; expansion 005D740F. Multiplayer house control selects
    // mission 5 (Guard) for a human, or 11 (Area Guard) for an AI.
    return human ? StartingMission::Guard : StartingMission::AreaGuard;
}

std::uint16_t finish_starting_unit_generation(sim::Random& random) {
    // Base 005B7F4B; expansion 005D6FEB. The original draws for a debug log.
    return static_cast<std::uint16_t>(random.ranged(0,65535));
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
std::optional<BattlePlan> plan_battle(const BattlePlanInput& input, sim::Random& random,
    const PrepareBattleWorld& prepare_world, const PlaceBattleUnit& place_unit,
    const FinishBattleHouse& finish_house, const FinishBattleWorld& finish_world) {
    // 00686B20 performs the first start assignment before map loading.
    // 00686890 then calls Battle +84 and 005D6D80 in that order. The caller
    // supplies the first result; the world phase resolves the intervening work.
    if (!prepare_world || !place_unit || !finish_house || !finish_world ||
        input.credits<0 || input.unit_count<0 ||
        input.houses.empty() || input.houses.size()>8 || input.houses[0].slot!=0 ||
        input.assigned_starts.size()!=input.houses.size())
        return std::nullopt;
    std::uint32_t colors=0, owner_mask=0;
    unsigned previous_slot=0;
    for (std::size_t i=0;i<input.houses.size();++i) {
        const auto& house=input.houses[i];
        if (house.slot>7 || (i && house.slot<=previous_slot) || house.country_index>=32 ||
            house.raw_color<0 || house.raw_color>7 || house.difficulty<0 || house.difficulty>2 ||
            (!i && house.difficulty!=1) || !house.unit_owner_bit ||
            (house.unit_owner_bit & (house.unit_owner_bit-1))) return std::nullopt;
        const auto color_bit=std::uint32_t{1}<<house.raw_color;
        if (colors & color_bit) return std::nullopt;
        colors|=color_bit;
        owner_mask|=house.unit_owner_bit;
        previous_slot=house.slot;
        if (input.bases_enabled && !select_base_unit(input.base_units,house.country_index))
            return std::nullopt;
    }
    const auto budget=starting_unit_budget(input.unit_count,input.global_tech_level,
                                           owner_mask,input.starting_types);
    if (!budget) return std::nullopt;
    auto working=random;
    BattlePlan plan{{},*budget};
    for (std::size_t i=0;i<input.houses.size();++i) {
        const auto& house=input.houses[i];
        plan.houses.push_back({house,initialize_house_values(input.credits,house.raw_color),
                               input.assigned_starts[i],0,0,0,starting_unit_mission(house.slot==0),
                               std::nullopt,{}});
    }
    const auto prepared=prepare_world(plan.houses,working);
    if (!prepared || !prepared->resolved_types_unchanged || prepared->active_slots.size()!=plan.houses.size() ||
        prepared->starts.size()!=plan.houses.size() ||
        prepared->cash.size()!=plan.houses.size()) return std::nullopt;
    for (std::size_t i=0;i<plan.houses.size();++i) {
        auto& house=plan.houses[i];
        if (prepared->active_slots[i]!=house.input.slot || prepared->starts[i].x!=house.start.x ||
            prepared->starts[i].y!=house.start.y) return std::nullopt;
        house.cash_before_units=prepared->cash[i];
        if (input.bases_enabled) {
            const auto index=select_base_unit(input.base_units,house.input.country_index);
            if (!index) return std::nullopt;
            const auto placed=place_unit(i,true,*index,house.start,working);
            if (!placed || !placed->placed) return std::nullopt;
            house.base_unit=StartingUnit{*index,placed->cell};
        }
        const auto generated=generate_starting_units(*budget,house.input.tech_level,
            house.input.unit_owner_bit,input.starting_types,working,
            [&](std::size_t type,sim::Random& state) {
                return place_unit(i,false,type,house.start,state);
            });
        if (!generated) return std::nullopt;
        house.extra_units=generated->placed;
        // 005D6D80 adds only a positive unused budget to that house's cash.
        const auto cash=std::int64_t(house.cash_before_units)+std::max(0,generated->remaining_budget);
        if (cash<std::numeric_limits<std::int32_t>::min() || cash>std::numeric_limits<std::int32_t>::max())
            return std::nullopt;
        // 005D6FA1..005D6FDA invokes four house updates before the next house.
        const auto finished=finish_house(i,static_cast<std::int32_t>(cash),working);
        if (!finished) return std::nullopt;
        house.cash_after_units=*finished;
    }
    finish_starting_unit_generation(working);
    const auto finished=finish_world(plan.houses,working);
    if (!finished || !finished->resolved_types_unchanged || !finished->staged_units_unchanged ||
        finished->active_slots.size()!=plan.houses.size() || finished->starts.size()!=plan.houses.size() ||
        finished->cash.size()!=plan.houses.size()) return std::nullopt;
    for (std::size_t i=0;i<plan.houses.size();++i) {
        auto& house=plan.houses[i];
        if (finished->active_slots[i]!=house.input.slot || finished->starts[i].x!=house.start.x ||
            finished->starts[i].y!=house.start.y) return std::nullopt;
        house.cash_after_world=finished->cash[i];
    }
    random=working;
    return plan;
}

}
