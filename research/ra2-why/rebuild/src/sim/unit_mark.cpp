#include "unit_mark.h"
#include "../weapons/occupant_order.h"
#include <algorithm>
#include <exception>
namespace ra2::sim {
namespace {
using World=session::world::Snapshot;
using Entity=session::world::Entity;
Entity* unit(World& world,std::uint32_t id) {
    const auto it=std::find_if(world.entities.begin(),world.entities.end(),[&](const Entity& e){return e.id==id;});
    return it==world.entities.end()?nullptr:&*it;
}
UnitMarkResult missing(const char* text) { return {UnitMarkStatus::NeedsInput,false,text}; }
UnitMarkResult unsupported(const char* text) { return {UnitMarkStatus::Unsupported,false,text}; }
}
UnitMarkResult original_unit_mark(World& world,std::uint32_t id,int mode,const UnitMarkCallbacks& cb) {
    if (mode==2) return {UnitMarkStatus::Complete,true,{}}; // 004C2E60 returns before the parent call.
    if (mode!=0 && mode!=1 && mode!=3) return unsupported("Unit mark mode is outside the connected slice");
    auto staged=world;auto* actor=unit(staged,id);
    if (!actor || actor->kind!=session::world::Kind::Unit || actor->lifetime!=session::world::Lifetime::Placed)
        return unsupported("Unit mark needs a placed Unit");
    if (!actor->object_field_74 || !actor->object_field_75 || !actor->object_field_76 || !actor->techno_field_370)
        return missing("Object mark bytes or Techno byte are unresolved");
    if (*actor->object_field_76) return {UnitMarkStatus::Complete,false,{}};
    try {
        if (cb.object_pre_mark) {
            const auto before=cb.object_pre_mark(staged,id,mode);
            if (!before || !*before) return missing("Object pre-mark effects are unresolved");
        } else {
            // 005D4160 calls 0040DC20, then the Unit +2A0, +38 and +1A0
            // getters. For an ordinary Unit these read Type, House and position;
            // their results are discarded and none writes world state.
            const auto house=std::find_if(staged.houses.begin(),staged.houses.end(),
                [&](const auto& h){return h.id==actor->house_id;});
            if (!actor->original_type_index || !actor->category || actor->category->runtime_type!=1 ||
                house==staged.houses.end() || !actor->position || !actor->cell || actor->position->x<0 || actor->position->y<0 ||
                actor->position->x/256!=actor->cell->x || actor->position->y/256!=actor->cell->y)
                return missing("Object pre-mark Unit getters are unresolved");
        }
        actor=unit(staged,id);
        if (!actor || !actor->object_field_74 || !actor->object_field_75 || !actor->techno_field_370) return missing("Object mark state changed to unknown");
        const bool adding=mode!=0;
        if (adding==(*actor->object_field_74!=0)) {
            // Parent Object mark returns false after its pre-mark effects.
            world=std::move(staged);return {UnitMarkStatus::Complete,false,{}};
        }
        *actor->object_field_74=adding?1:0;
        if (adding && !*actor->object_field_75) {
            // Unit virtual +12C is 005D36E0. It sets +75 before 004E09A0(0).
            *actor->object_field_75=1;
            if (!cb.refresh_after_mark) return missing("Object refresh virtual is unresolved");
            const auto refreshed=cb.refresh_after_mark(staged,id);
            if (!refreshed || !*refreshed) return missing("Object refresh virtual is unresolved");
            actor=unit(staged,id);
            if (!actor || !actor->techno_field_370) return missing("Techno byte changed to unknown");
        }
        if (*actor->techno_field_370) {
            if (!cb.techno_dirty) return missing("Techno mark side effect is unresolved");
            const auto done=cb.techno_dirty(staged,id);
            if (!done || !*done) return missing("Techno mark side effect is unresolved");
            actor=unit(staged,id);
        }
        if (!actor || !actor->base_profile || !actor->base_profile->type_flag_205 || !actor->cell ||
            !actor->position || !actor->layer || !actor->occupancy_tracking || !actor->next)
            return missing("Unit mark type, Cell, position, layer, or occupancy state is unresolved");
        if (!*actor->base_profile->type_flag_205) { world=std::move(staged);return {UnitMarkStatus::Complete,true,{}}; }
        if (*actor->layer!=session::world::Layer::Ground || actor->cell->x>=512 || actor->cell->y>=512)
            return unsupported("Unit mark bridge layer is not recovered");
        const auto cell_id=std::uint32_t(actor->cell->y)*512+actor->cell->x;
        auto it=staged.cells.find(cell_id);
        if (it==staged.cells.end() || !it->second.allocated || !*it->second.allocated || !it->second.heads ||
            !it->second.terrain || !it->second.terrain->floor_world_height || !it->second.terrain->ramp_index ||
            !it->second.cell_flag_100 || !it->second.ground_occupancy_124 || !it->second.bridge_occupancy_128 || !cb.bridge_height)
            return missing("Unit mark Cell, height, or occupancy state is unresolved");
        if (!it->second.cell_flags_140 || !it->second.cell_flag_200 || !it->second.cell_flag_40000 ||
            !it->second.cell_flag_20000 || !it->second.cell_field_11d || !it->second.cell_field_12c || !it->second.neighbor_count ||
            !it->second.coarse_category || !it->second.coarse_cache_height || !it->second.hierarchy_cache_height)
            return missing("Unit mark Cell flags or setup cache are unresolved");
        if (*it->second.cell_flag_100 || *it->second.terrain->ramp_index || *cb.bridge_height<=0 ||
            std::int64_t(actor->position->z)>=std::int64_t(*it->second.terrain->floor_world_height)+*cb.bridge_height)
            return unsupported("Unit mark bridge or ramp occupancy is not recovered");
        weapons::ContentLinks links;links.cells.emplace(cell_id,*it->second.heads);
        for (const auto& e:staged.entities) {
            if (!e.next) return missing("ordered Object link is unresolved");
            links.objects.emplace(e.id,weapons::ContentObject{*e.next,e.kind==session::world::Kind::Structure?6:1});
        }
        const auto result=adding?links.insert(cell_id,weapons::ContentLayer::Ground,id):links.remove(cell_id,weapons::ContentLayer::Ground,id);
        if (result!=weapons::ContentResult::Complete) return unsupported("ordered Cell link contains a cycle");
        it->second.heads=links.cells.at(cell_id);
        for (auto& e:staged.entities) e.next=links.objects.at(e.id).next;
        if (adding) {
            if (!cb.insertion_visibility) return missing("Cell insertion visibility effects are unresolved");
            const auto visible=cb.insertion_visibility(staged,id,cell_id);
            if (!visible || !*visible) return missing("Cell insertion visibility effects are unresolved");
        }
        actor=unit(staged,id);it=staged.cells.find(cell_id);
        if (!actor || !actor->occupancy_tracking || it==staged.cells.end() || !it->second.ground_occupancy_124)
            return missing("Unit occupancy changed to unknown");
        if (*actor->occupancy_tracking) {
            if (adding) *it->second.ground_occupancy_124|=0x20u;
            else *it->second.ground_occupancy_124&=~0x20u;
        }
        if (!cb.cell_setup) return missing("Cell setup after marking is unresolved");
        const auto setup=cb.cell_setup(staged,cell_id);
        if (!setup || !*setup) return missing("Cell setup after marking is unresolved");
        it=staged.cells.find(cell_id);
        if (it==staged.cells.end() || !it->second.heads || !it->second.cell_flags_140 ||
            !it->second.ground_occupancy_124 || !it->second.bridge_occupancy_128 || !it->second.cell_field_12c || !it->second.cell_flag_100 ||
            !it->second.cell_flag_200 || !it->second.cell_flag_40000 || !it->second.cell_flag_20000 ||
            !it->second.coarse_category || !it->second.coarse_cache_height || !it->second.hierarchy_cache_height)
            return missing("Cell setup returned unknown state");
        if (!session::world::validate(staged)) return missing("Cell setup returned invalid links or flags");
        world=std::move(staged);return {UnitMarkStatus::Complete,true,{}};
    } catch(const std::exception&) { return unsupported("original Unit mark callback or link operation failed"); }
}
}
