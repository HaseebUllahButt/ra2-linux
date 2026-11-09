#include "damage_runtime.h"
#include <map>
#include <set>

namespace ra2::sim {
WorldCollisionResult base_world_projectile_collision(
    const session::world::Snapshot& world, std::uint32_t cell_index,
    Coord position, std::uint32_t source_id, bool preserve,
    const CollisionLookup& resolve, const OriginalDistance& distance) {
    auto missing=[](const char* reason) { return WorldCollisionResult{{},reason}; };
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup)
        return missing("base world setup is incomplete");
    if (world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second)
        return missing("random state indexes are invalid");
    if (!resolve) return missing("collision resolver is missing");
    const auto cell=world.cells.find(cell_index);
    if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated ||
        !cell->second.heads)
        return missing("allocated collision cell or its ordered heads are unknown");
    std::map<std::uint32_t,const session::world::Entity*> entities;
    for (const auto& entity:world.entities)
        if (!entity.id || !entities.emplace(entity.id,&entity).second)
            return missing("object IDs are invalid");
    if (source_id && !entities.count(source_id)) return missing("projectile source is unknown");
    std::set<std::uint32_t> seen;
    std::vector<weapons::ProjectileCellObject> candidates;
    std::map<std::uint32_t,CollisionProperties> properties;
    for (auto id=cell->second.heads->ground_head; id;) {
        const auto it=entities.find(id);
        if (it==entities.end() || !seen.insert(id).second ||
            it->second->lifetime!=session::world::Lifetime::Placed)
            return missing("ordered ground object chain is invalid");
        const auto& entity=*it->second;
        if (!entity.next) return missing("object next link is unknown");
        auto p=resolve(entity,source_id);
        if (!p || !p->eligible) return missing("object collision flag is unknown");
        if (*p->eligible && !p->virtual_position)
            return missing("object virtual position is unknown");
        properties.emplace(id,*p);
        candidates.push_back({id,p->virtual_position.value_or(Coord{}),
                              entity.position.value_or(Coord{}),*p->eligible,false});
        id=*entity.next;
    }
    // Resolve the selected candidate before asking for its alliance and stored
    // position. The original selects first, then checks the source/alliance.
    auto selected=weapons::base_projectile_object_collision(position,source_id,
                                                               preserve,candidates,distance);
    if (!selected.selected_id || selected.selected_id==source_id)
        return {selected,{}};
    const auto* object=entities.at(selected.selected_id);
    auto& p=properties.at(selected.selected_id);
    if (source_id) {
        if (!p.allied_to_source) return missing("selected object alliance is unknown");
        if (*p.allied_to_source) {
            selected.collision=false; selected.position=position;
            return {selected,{}};
        }
    }
    if (!object->position) return missing("selected object stored position is unknown");
    for (auto& candidate:candidates)
        if (candidate.id==selected.selected_id) candidate.stored_position=*object->position;
    return {weapons::base_projectile_object_collision(position,source_id,preserve,
                                                       candidates,distance),{}};
}
}

#include "fp.h"
#include <array>
#include <cmath>

namespace ra2::sim {
AreaCollectionResult base_small_ground_area_candidates(
    const session::world::Snapshot& world, Coord impact, float spread,
    std::uint32_t source_id, bool type_filter,
    const AreaUnitLookup& resolve, const OriginalDistance& distance) {
    auto missing=[](const char* reason) { return AreaCollectionResult{{},reason}; };
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second)
        return missing("base world setup or random state is incomplete");
    if (!resolve || !std::isfinite(spread) || spread<0 || spread>0.5f)
        return missing("area inputs are outside the recovered small-spread slice");
    const auto cx=impact.x/256, cy=impact.y/256;
    if (cx<0 || cx>=512 || cy<0 || cy>=512)
        return missing("impact needs the original fallback cell");
    const auto center=world.cells.find(static_cast<unsigned>(cy*512+cx));
    if (center==world.cells.end() || !center->second.terrain ||
        !center->second.terrain->floor_world_height || !center->second.terrain->ramp_index ||
        *center->second.terrain->ramp_index!=0 || !center->second.cell_flag_100 ||
        *center->second.cell_flag_100 || impact.z>*center->second.terrain->floor_world_height)
        return missing("aerial or bridge area collection is unresolved");
    // DAT_00A6F248 is initialized by 005441E0 in this order. Count is
    // DAT_007A5908[trunc(CellSpread + 0.99)].
    constexpr std::array<std::array<int,2>,9> offsets{{
        {{0,0}},{{1,-1}},{{0,-1}},{{-1,-1}},{{-1,0}},
        {{1,0}},{{-1,1}},{{0,1}},{{1,1}}}};
    const auto count=static_cast<int>(fp::add(static_cast<double>(spread),0.99)) ? 9u : 1u;
    std::map<std::uint32_t,const session::world::Entity*> entities;
    for (const auto& entity:world.entities)
        if (!entity.id || !entities.emplace(entity.id,&entity).second)
            return missing("object IDs are invalid");
    if (source_id && !entities.count(source_id)) return missing("area source is unknown");
    std::vector<AreaCandidate> result;
    for (unsigned i=0;i<count;++i) {
        const int x=cx+offsets[i][0], y=cy+offsets[i][1];
        if (x<0 || x>=512 || y<0 || y>=512)
            return missing("area needs the original fallback cell");
        const auto cell=world.cells.find(static_cast<unsigned>(y*512+x));
        if (cell==world.cells.end() || !cell->second.allocated || !*cell->second.allocated ||
            !cell->second.heads || !cell->second.terrain ||
            !cell->second.terrain->overlay_type_index || *cell->second.terrain->overlay_type_index!=-1)
            return missing("area cell or overlay state is unknown");
        std::set<std::uint32_t> seen;
        for (auto id=cell->second.heads->ground_head;id;) {
            const auto found=entities.find(id);
            if (found==entities.end() || !seen.insert(id).second ||
                found->second->lifetime!=session::world::Lifetime::Placed || !found->second->next)
                return missing("area object chain is incomplete or cyclic");
            const auto& object=*found->second;
            const auto next=*object.next;
            if (id==source_id) { id=next; continue; }
            auto p=resolve(object);
            if (!p || !p->active) return missing("area object active state is unknown");
            if (!*p->active) { id=next; continue; }
            if (object.kind!=session::world::Kind::Unit)
                return missing("non-Unit area distance or callbacks are unresolved");
            if (type_filter) {
                if (!p->excluded_by_scenario) return missing("area type exclusion is unknown");
                if (*p->excluded_by_scenario) { id=next; continue; }
            }
            if (!p->damage_position) return missing("area virtual damage position is unknown");
            const auto& q=*p->damage_position;
            const auto safe_delta=[](int a,int b) {
                const auto d=static_cast<long long>(a)-b;
                return d>=-1048576 && d<=1048576;
            };
            if (!safe_delta(impact.x,q.x) || !safe_delta(impact.y,q.y) || !safe_delta(impact.z,q.z))
                return missing("area distance exceeds the verified integer domain");
            const int d=distance.between(impact,q);
            if (i==0) {
                if (!p->special_flag) return missing("area special-group flag is unknown");
                if (*p->special_flag) {
                    if (!p->special_virtual) return missing("area special-group virtual result is unknown");
                    if (*p->special_virtual && d<0x55)
                        return missing("area special-group dispatch is unresolved");
                }
            }
            result.push_back({id,d});
            id=next;
        }
    }
    return {std::move(result),{}};
}
}

namespace ra2::sim {
weapons::FlatCannonTickStatus base_advance_flat_projectile(
    FlatProjectile& projectile, const weapons::FlatCannonTickInput& tick,
    const weapons::TerrainTraceLookup& terrain, const OriginalDistance& distance) {
    using S=weapons::FlatCannonTickStatus;
    if (projectile.phase!=FlatProjectilePhase::Flying ||
        projectile.arc.position!=tick.arc.position || projectile.arc.velocity!=tick.arc.velocity ||
        projectile.arc.gravity!=tick.arc.gravity || projectile.source_id!=tick.source_id ||
        projectile.target.x!=tick.target.x || projectile.target.y!=tick.target.y ||
        projectile.target.z!=tick.target.z || !terrain)
        return S::NeedsInput;
    const auto result=weapons::base_flat_cannon_tick(tick,terrain,distance);
    if (result.status==S::NeedsInput) return result.status;
    projectile.arc.position=result.state.position;
    projectile.arc.velocity=result.state.velocity;
    if (result.status==S::Impact) projectile.phase=FlatProjectilePhase::ImpactPending;
    else if (result.status==S::Remove) projectile.phase=FlatProjectilePhase::RemovalPending;
    return result.status;
}
}

namespace ra2::sim {
std::optional<Coord> base_stationary_unit_aim(const session::world::Snapshot& world,
    std::uint32_t source_id,std::uint32_t target_id,std::optional<bool> moving) {
    if (world.edition!=session::world::Edition::Base || world.pending_cell_setup ||
        world.random.first>=250 || world.random.second>=250 ||
        (world.random.first+103)%250!=world.random.second || !source_id || !target_id ||
        !moving || *moving) return {};
    const session::world::Entity* source=nullptr;const session::world::Entity* target=nullptr;
    std::set<std::uint32_t> seen;
    for (const auto& item:world.entities) {
        if (!item.id || !seen.insert(item.id).second) return {};
        if (item.id==source_id) source=&item;
        if (item.id==target_id) target=&item;
    }
    if (!source || !target || source->kind!=session::world::Kind::Unit ||
        target->kind!=session::world::Kind::Unit || source->lifetime!=session::world::Lifetime::Placed ||
        target->lifetime!=session::world::Lifetime::Placed || !target->position) return {};
    return target->position;
}
}

namespace ra2::sim {
AreaDamageNext base_next_area_damage(AreaDamageCursor& cursor,const AreaRecipientLookup& lookup) {
    using S=AreaDamageStep;
    if (!lookup || cursor.radius<0 || !cursor.event.area_flag || !cursor.event.warhead_id ||
        cursor.next>cursor.candidates.size()) return {S::NeedsInput,{}};
    if (cursor.pending) return {S::Request,cursor.pending};
    while (cursor.next<cursor.candidates.size()) {
        const auto candidate=cursor.candidates[cursor.next];
        if (!candidate.id || candidate.distance<0) return {S::NeedsInput,{}};
        auto state=lookup(candidate.id);
        if (!state || state->target_id!=candidate.id) return {S::NeedsInput,{}};
        state->attacker_id=cursor.event.attacker_id;
        state->warhead_id=cursor.event.warhead_id;
        state->raw_damage=cursor.event.damage;
        state->distance=candidate.distance;
        state->radius=cursor.radius;
        // 0047E2E8 checks the scenario gate once, before collection. A
        // trigger may change that global later; do not recheck it here.
        state->scenario_blocks_damage=false;
        const auto request=weapons::base_resolved_impact_recipient(*state);
        if (request) { cursor.pending=request; return {S::Request,request}; }
        ++cursor.next;
    }
    return {S::Complete,{}};
}
bool base_complete_area_damage_request(AreaDamageCursor& cursor,std::uint32_t id) {
    if (!cursor.pending || !id || cursor.pending->target_id!=id || cursor.next>=cursor.candidates.size() ||
        cursor.candidates[cursor.next].id!=id) return false;
    cursor.pending.reset();++cursor.next;return true;
}
}
