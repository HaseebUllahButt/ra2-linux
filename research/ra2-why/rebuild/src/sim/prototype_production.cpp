#include "prototype_production.h"
#include "session/world/world.h"
#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace ra2::sim::production::prototype {
namespace {
namespace w=ra2::session::world;

Outcome fail(Status status,std::string detail) { return {status,std::move(detail),{}, {}}; }
bool ready(const w::Snapshot& world) {
    return world.edition==w::Edition::Base && world.setup_progress==w::SetupProgress::PrototypeComplete &&
        !world.pending_cell_setup && world.geometry.has_value();
}
w::Entity* entity(w::Snapshot& world,std::uint32_t id) {
    for (auto& item:world.entities) if (item.id==id) return &item;
    return nullptr;
}
const w::Entity* entity(const w::Snapshot& world,std::uint32_t id) {
    for (const auto& item:world.entities) if (item.id==id) return &item;
    return nullptr;
}
w::House* house(w::Snapshot& world,std::uint32_t id) {
    for (auto& item:world.houses) if (item.id==id) return &item;
    return nullptr;
}
// TODO-ORIGINAL: the exact checked functions require Complete. This adapter
// uses a private candidate with prototype inputs, then restores the marker.
template<class F>
Outcome transaction(w::Snapshot& world,FactorySet& factories,F&& callback) {
    if (!ready(world)) return fail(Status::NeedsInput,"prototype setup is not complete");
    auto candidate=world;
    auto next_factories=factories;
    candidate.setup_progress=w::SetupProgress::Complete;
    auto result=callback(candidate,next_factories);
    if (result.status!=Status::Done) return result;
    candidate.setup_progress=w::SetupProgress::PrototypeComplete;
    if (!w::validate(candidate)) return fail(Status::NeedsInput,"prototype result has invalid links");
    world=std::move(candidate);
    factories=std::move(next_factories);
    return result;
}

// TODO-ORIGINAL: replace these fields with the real unlimbo and mark effects.
bool activate(w::Snapshot& world,w::Entity& item) {
    if (!item.cell || !item.position || !item.resolved_hp || *item.resolved_hp<=0) return false;
    const auto key=std::uint32_t(item.cell->y)*512+item.cell->x;
    const auto found=world.cells.find(key);
    if (found==world.cells.end() || !found->second.terrain || !found->second.terrain->floor_world_height) return false;
    item.position->z=*found->second.terrain->floor_world_height;
    item.active=true; item.in_limbo=false; item.on_map=true; item.collision_eligible=true;
    item.collision_virtual_position=item.position; item.has_trigger=false; item.target_id=0;
    item.constructor_complete=true; item.object_field_74=1; item.object_field_75=1; item.object_field_76=0;
    item.weapon_timer_start_frame=-1; item.weapon_timer_duration=0;
    item.firepower_multiplier=1.0; item.armor_multiplier=1.0; item.house_armor_multiplier=1.0;
    item.veterancy=0.0; item.techno_field_1b8=0; item.techno_field_329=0;
    if (item.kind==w::Kind::Unit || item.kind==w::Kind::Infantry) {
        item.locomotor_moving=false; item.occupancy_tracking=true; item.object_field_75=1;
        item.techno_field_370=0; item.unit_field_7d=1; item.foot_field_5a1=0;
        item.mission=0; item.burst_index=0; item.hull_facing=0; item.hull_facing_raw=0;
        item.turret_facing=0; item.turret_facing_raw=0;
        item.category.emplace(); item.category->runtime_type=item.kind==w::Kind::Unit ? 1 : 15;
    }
    return true;
}

const w::Entity* choose_factory(const w::Snapshot& world,const FactorySet& factories,const Catalog& catalog,
                                std::uint32_t owner,Rtti wanted) {
    const w::Entity* fallback=nullptr;
    for (const auto& object:world.creation_order) {
        if (object.kind!=w::CreatedKind::Entity) continue;
        const auto* building=entity(world,object.id);
        if (!building || building->kind!=w::Kind::Structure || building->house_id!=owner ||
            building->lifetime!=w::Lifetime::Placed || !building->cell || building->object_field_76!=0) continue;
        const auto* type=catalog.find(building->type);
        const auto state=factories.buildings.find(building->id);
        if (!type || type->factory!=wanted || state==factories.buildings.end()) continue;
        if (state->second.primary) return building;
        fallback=building;
    }
    return fallback;
}

// TODO-ORIGINAL: replace this free-cell search with Building::Exit_Object,
// Find_Exit_Cell, radio messages, locomotor and infantry subcell handling.
std::optional<ra2map::Cell> exit_cell(const w::Snapshot& world,const Catalog& catalog,const w::Entity& building) {
    if (!building.cell) return {};
    const auto* type=catalog.find(building.type);
    if (!type || type->placement.foundation<0 || type->placement.foundation>=22) return {};
    const auto& footprint=original_foundation_cells(type->placement.foundation);
    if (footprint.empty()) return {};
    int left=building.cell->x,right=left,top=building.cell->y,bottom=top;
    for (const auto& offset:footprint) {
        left=std::min(left,int(building.cell->x)+offset.x);
        right=std::max(right,int(building.cell->x)+offset.x);
        top=std::min(top,int(building.cell->y)+offset.y);
        bottom=std::max(bottom,int(building.cell->y)+offset.y);
    }
    for (int radius=1;radius<=16;++radius) {
        for (int y=top-radius;y<=bottom+radius;++y) for (int x=left-radius;x<=right+radius;++x) {
            if (x!=left-radius && x!=right+radius && y!=top-radius && y!=bottom+radius) continue;
            if (x<0 || y<0 || x>=512 || y>=512) continue;
            const auto it=world.cells.find(std::uint32_t(y)*512+x);
            if (it==world.cells.end()) continue;
            const auto& cell=it->second;
            if (cell.allocated!=true || !cell.heads || cell.heads->ground_head || cell.heads->bridge_head ||
                !cell.ground_occupancy_124 || *cell.ground_occupancy_124 || !cell.terrain ||
                !cell.terrain->floor_world_height || !cell.terrain->land_type ||
                *cell.terrain->land_type==2 || *cell.terrain->land_type==3 ||
                cell.terrain->overlay_type_index!= -1 || cell.terrain->bridge!=false ||
                (cell.unit_land_speed && *cell.unit_land_speed<=0)) continue;
            return ra2map::Cell{static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)};
        }
    }
    return {};
}

// The completed FactoryState owns its object until a free exit is found.
// A failed exit keeps the object and lets the next tick try again.
Outcome discharge(w::Snapshot& world,FactorySet& factories,const Catalog& catalog) {
    Outcome result{Status::Done,{},{},{}};
    for (auto& factory:factories.factories) {
        if (!factory.object || factory.progress!=54 || (factory.slot!=1 && factory.slot!=2)) continue;
        auto* item=entity(world,factory.object);
        const auto wanted=factory.slot==1 ? Rtti::InfantryType : Rtti::UnitType;
        const auto kind=factory.slot==1 ? w::Kind::Infantry : w::Kind::Unit;
        if (!item || item->kind!=kind || item->lifetime!=w::Lifetime::Created || item->house_id!=factory.house_id)
            return fail(Status::NeedsInput,"completed factory object is invalid");
        const auto* building=choose_factory(world,factories,catalog,factory.house_id,wanted);
        if (!building) { result.detail="waiting for a factory exit"; continue; }
        const auto cell=exit_cell(world,catalog,*building);
        if (!cell) { result.detail="waiting for a clear factory exit"; continue; }
        auto& target=world.cells.at(std::uint32_t(cell->y)*512+cell->x);
        item->cell=*cell; item->layer=w::Layer::Ground; item->lifetime=w::Lifetime::Placed;
        item->position=sim::Coord{int(cell->x)*256+128,int(cell->y)*256+128,*target.terrain->floor_world_height};
        item->next=target.heads->ground_head;
        target.heads->ground_head=item->id;
        // TODO-ORIGINAL: Infantry uses real subcell bits. Prototype movement
        // currently reserves bit 20 for both movable kinds.
        *target.ground_occupancy_124|=0x20;
        if (!activate(world,*item)) return fail(Status::NeedsInput,"factory object cannot be activated");
        if (kind==w::Kind::Unit && item->original_type_index) {
            auto* owner=house(world,item->house_id);
            if (!owner || !owner->placed_unit_counts) return fail(Status::NeedsInput,"placed Unit counts are unknown");
            auto& count=(*owner->placed_unit_counts)[*item->original_type_index];
            if (count==std::numeric_limits<std::int32_t>::max())
                return fail(Status::NeedsInput,"placed Unit count is too large");
            ++count;
        }
        result.entity=item->id;
        factory.object=0; factory.object_type.clear(); factory.suspended=true;
        factory.progress=0; factory.interval=0; factory.balance=0;
    }
    return result;
}
}

Outcome initialize(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,std::uint32_t player_id) {
    if (!ready(world) || !w::validate(world) || !house(world,player_id))
        return fail(Status::NeedsInput,"prototype world or player is invalid");
    auto candidate=world;
    auto next=factories;
    // TODO-ORIGINAL: replace these defaults with original multiplayer session,
    // House, factory, power, and building initialization phases.
    if (!next.flags.session_type) next.flags.session_type=1;
    if (!next.flags.super_weapons) next.flags.super_weapons=true;
    if (!next.flags.build_off_ally) next.flags.build_off_ally=false;
    if (next.flags.player_house && *next.flags.player_house!=player_id)
        return fail(Status::Rejected,"player House differs from initialized session");
    next.flags.player_house=player_id;
    for (auto& item:candidate.houses) {
        if (item.role!=w::HouseRole::Player) continue;
        if (!item.country_index) {
            const auto found=std::find(catalog.countries.begin(),catalog.countries.end(),item.country);
            if (found==catalog.countries.end()) return fail(Status::NeedsInput,"player country is missing");
            item.country_index=static_cast<std::int32_t>(found-catalog.countries.begin());
        }
        if (!item.cash || !item.human_controlled || !item.original_index)
            return fail(Status::NeedsInput,"player House state is missing");
        if (!item.tech_level) item.tech_level=10;
        if (!item.house_flag_135) item.house_flag_135=false;
        if (!item.stolen_tech_204) item.stolen_tech_204=false;
        if (!item.stolen_tech_205) item.stolen_tech_205=false;
        if (!item.stolen_mask_208) item.stolen_mask_208=0;
        if (!item.stolen_mask_20c) item.stolen_mask_20c=0;
        if (!item.stolen_mask_210) item.stolen_mask_210=0;
        if (!item.stolen_mask_214) item.stolen_mask_214=0;
        if (!item.power_output) item.power_output=0;
        if (!item.power_drain) item.power_drain=0;
        if (!item.placed_unit_counts) item.placed_unit_counts.emplace();
        for (auto* counts:{&item.buildings_active_5460,&item.buildings_owned_5420,&item.buildings_ever_54a0,
             &item.units_owned_5430,&item.units_ever_54b0,&item.infantry_owned_5440,&item.infantry_ever_54c0,
             &item.aircraft_owned_5450,&item.aircraft_ever_54d0}) if (!*counts) counts->emplace();
        next.house_factory_counts.try_emplace(item.id,std::array<std::int32_t,6>{});
        next.power_blackout_idle.try_emplace(item.id,true);
        next.power_dirty.try_emplace(item.id,false);
    }
    for (auto& item:candidate.entities) {
        if (item.kind!=w::Kind::Structure || item.lifetime!=w::Lifetime::Placed) continue;
        if (!catalog.find(item.type)) continue;
        next.buildings.try_emplace(item.id,BuildingState{});
    }
    if (!w::validate(candidate)) return fail(Status::NeedsInput,"prototype links are invalid");
    world=std::move(candidate);
    factories=std::move(next);
    return {Status::Done,{},{},{}};
}

Outcome deploy(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,
               std::uint32_t unit_id,std::optional<bool> can_deploy) {
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        auto* unit=entity(candidate,unit_id);
        if (!unit) return fail(Status::Rejected,"unit is missing");
        const auto owner_id=unit->house_id;
        auto* owner=house(candidate,owner_id);
        if (!owner || !owner->human_controlled) return fail(Status::NeedsInput,"House control is unknown");
        const bool human=*owner->human_controlled;
        // TODO-ORIGINAL: the exact computer base-planning call is unresolved.
        // Use the human deploy branch for this prototype, then restore the flag.
        owner->human_controlled=true;
        auto result=ra2::sim::production::deploy(candidate,next,catalog,unit_id,can_deploy);
        if (result.status==Status::NeedsTurn && result.value && *result.value>=0 && *result.value<=255) {
            unit=entity(candidate,unit_id);
            if (!unit) return fail(Status::NeedsInput,"unit vanished before turning");
            // TODO-ORIGINAL: animate the original turn and retry when it ends.
            unit->hull_facing=*result.value;
            unit->hull_facing_raw=static_cast<std::uint16_t>(*result.value<<8);
            result=ra2::sim::production::deploy(candidate,next,catalog,unit_id,can_deploy);
        }
        owner=house(candidate,owner_id);
        if (!owner) return fail(Status::NeedsInput,"House vanished during deploy");
        owner->human_controlled=human;
        if (result.status!=Status::Done || !result.entity) return result;
        unit=entity(candidate,unit_id);
        auto* building=entity(candidate,*result.entity);
        if (!unit || !building || !activate(candidate,*building))
            return fail(Status::NeedsInput,"deployed building is incomplete");
        unit->active=false; unit->in_limbo=true; unit->collision_eligible=false;
        return result;
    });
}

std::vector<std::pair<std::string,CanBuild>> options(const w::Snapshot& world,const FactorySet& factories,
                                                     const Catalog& catalog,std::uint32_t house_id) {
    if (!ready(world)) return {};
    return ra2::sim::production::options(world,factories,catalog,house_id);
}

Outcome begin_production(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,
                         std::uint32_t house_id,const std::string& type,std::uint64_t frame) {
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        return ra2::sim::production::begin_production(candidate,next,catalog,house_id,type,frame);
    });
}

std::optional<bool> passes_proximity(const w::Snapshot& world,const FactorySet& factories,
                                      const Catalog& catalog,std::uint32_t house_id,
                                      const std::string& type,CellXY cell) {
    if (!ready(world)) return {};
    return ra2::sim::production::passes_proximity(world,factories,catalog,house_id,type,cell);
}

Outcome place(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,std::uint32_t house_id,
              const std::string& type,CellXY cell,std::uint64_t frame) {
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        const auto proximity=ra2::sim::production::passes_proximity(candidate,next,catalog,house_id,type,cell);
        if (!proximity) return fail(Status::NeedsInput,"building proximity is unknown");
        if (!*proximity) return fail(Status::Rejected,"building is too far from the base");
        auto result=ra2::sim::production::place(candidate,next,catalog,house_id,type,cell,frame);
        if (result.status==Status::Done && result.entity) {
            auto* building=entity(candidate,*result.entity);
            if (!building || !activate(candidate,*building)) return fail(Status::NeedsInput,"building is incomplete");
        }
        return result;
    });
}

Outcome tick(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,std::uint64_t frame) {
    if (!ready(world)) return fail(Status::NeedsInput,"prototype setup is not complete");
    bool needed=false,waiting_for_exit=false;
    for (const auto& item:factories.power_dirty) if (item.second) { needed=true; break; }
    for (const auto& factory:factories.factories) {
        if (!factory.object) continue;
        if (factory.progress==54 && (factory.slot==1 || factory.slot==2)) {
            const auto* item=entity(world,factory.object);
            const auto expected=factory.slot==1 ? w::Kind::Infantry : w::Kind::Unit;
            if (!item || item->kind!=expected || item->lifetime!=w::Lifetime::Created ||
                item->house_id!=factory.house_id) return fail(Status::NeedsInput,"completed factory object is invalid");
            const auto wanted=factory.slot==1 ? Rtti::InfantryType : Rtti::UnitType;
            const auto* building=choose_factory(world,factories,catalog,factory.house_id,wanted);
            if (building && exit_cell(world,catalog,*building)) needed=true;
            else waiting_for_exit=true;
        }
        if (!factory.suspended && factory.progress<54 &&
            (frame<factory.timer_start || frame-factory.timer_start>=std::uint64_t(std::max(factory.interval,0)))) needed=true;
    }
    if (!needed) return {Status::Done,waiting_for_exit ? "waiting for a clear factory exit" : "",{}, {}};
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        auto result=ra2::sim::production::tick(candidate,next,catalog,frame);
        if (result.status!=Status::Done) return result;
        return discharge(candidate,next,catalog);
    });
}
}
