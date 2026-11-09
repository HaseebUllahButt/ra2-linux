#include "prototype_production.h"
#include "../loop/profile.h"
#include "session/world/world.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
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
// known_valid: the callback sets it only when it proved that validate()
// holds for the final world; the full check is then skipped.
template<class F>
Outcome transaction(w::Snapshot& world,FactorySet& factories,F&& callback,const bool* known_valid=nullptr) {
    loop::profile::Scope probe(loop::profile::Phase::Production);
    if (!ready(world)) return fail(Status::NeedsInput,"prototype setup is not complete");
    // Edit the live world; the Undo restores it unless the command commits.
    w::Undo undo(world);
    auto next_factories=factories;
    world.setup_progress=w::SetupProgress::Complete;
    auto result=callback(world,next_factories);
    if (result.status!=Status::Done) return result;
    world.setup_progress=w::SetupProgress::PrototypeComplete;
    if (!(known_valid && *known_valid) && !w::validate(world)) return fail(Status::NeedsInput,"prototype result has invalid links");
    undo.commit();
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
                                std::uint32_t owner,Rtti wanted,bool naval=false) {
    const w::Entity* fallback=nullptr;
    for (const auto& object:world.creation_order) {
        if (object.kind!=w::CreatedKind::Entity) continue;
        const auto* building=entity(world,object.id);
        if (!building || building->kind!=w::Kind::Structure || building->house_id!=owner ||
            building->lifetime!=w::Lifetime::Placed || !building->cell || building->object_field_76!=0 ||
            building->active==false || building->in_limbo==true ||
            (building->resolved_hp && *building->resolved_hp<=0)) continue;
        const auto* type=catalog.find(building->type);
        const auto state=factories.buildings.find(building->id);
        if (!type || type->factory!=wanted || state==factories.buildings.end() ||
            (wanted==Rtti::UnitType && type->placement.naval!=naval)) continue;
        if (state->second.primary) return building;
        fallback=building;
    }
    return fallback;
}

std::optional<Rtti> product_kind(std::int32_t slot) {
    switch (slot) {
    case 0: return Rtti::AircraftType;
    case 1: return Rtti::InfantryType;
    case 2: case 3: return Rtti::UnitType;
    case 4: case 5: return Rtti::BuildingType;
    default: return {};
    }
}

// TODO-ORIGINAL: restore the original Building destruction, FactoryClass
// cancellation, refund, and House tracking callbacks in their actual order.
Outcome cleanup_destroyed(w::Snapshot& world,FactorySet& factories,const Catalog& catalog) {
    std::set<std::uint32_t> affected;
    for (auto it=factories.buildings.begin();it!=factories.buildings.end();) {
        const auto* building=entity(world,it->first);
        if (!building) return fail(Status::NeedsInput,"tracked building is missing");
        if (building->lifetime!=w::Lifetime::Destroyed) { ++it; continue; }
        affected.insert(building->house_id);
        it=factories.buildings.erase(it);
    }
    for (const auto id:affected) {
        auto* owner=house(world,id);
        if (!owner) return fail(Status::NeedsInput,"destroyed building House is missing");
        if (owner->role!=w::HouseRole::Player) continue;
        std::map<std::int32_t,std::int32_t> active,owned,units,infantry,aircraft,unit_types,placed_units;
        std::int32_t unit_count=0;
        std::array<std::int32_t,6> counts{};
        for (const auto& item:world.entities) {
            if (item.house_id!=id || item.kind==w::Kind::Terrain || item.lifetime==w::Lifetime::Destroyed) continue;
            if (item.lifetime==w::Lifetime::Placed && (item.active==false || item.in_limbo==true ||
                (item.resolved_hp && *item.resolved_hp<=0))) continue;
            const auto* type=catalog.find(item.type);
            if (!type) return fail(Status::NeedsInput,"surviving actor type is missing");
            if (item.kind==w::Kind::Unit) {
                if (item.excluded_from_house_tracking==true) continue;
                ++units[type->heap_index]; ++unit_types[type->heap_index]; ++unit_count;
                if (item.lifetime==w::Lifetime::Placed) ++placed_units[type->heap_index];
                continue;
            }
            if (item.kind==w::Kind::Infantry) { ++infantry[type->heap_index]; continue; }
            if (item.kind==w::Kind::Aircraft) { ++aircraft[type->heap_index]; continue; }
            if (item.kind!=w::Kind::Structure || type->rtti!=Rtti::BuildingType)
                return fail(Status::NeedsInput,"surviving building type is missing");
            ++owned[type->heap_index];
            if (item.lifetime==w::Lifetime::Created) continue;
            ++active[type->heap_index];
            if (!type->factory) continue;
            switch (*type->factory) {
            case Rtti::AircraftType: ++counts[0]; break;
            case Rtti::InfantryType: ++counts[1]; break;
            case Rtti::UnitType: ++counts[type->placement.naval ? 4 : 2]; break;
            case Rtti::BuildingType: ++counts[3]; break;
            default: break;
            }
        }
        owner->buildings_active_5460=std::move(active);
        owner->buildings_owned_5420=std::move(owned);
        owner->units_owned_5430=std::move(units); owner->infantry_owned_5440=std::move(infantry);
        owner->aircraft_owned_5450=std::move(aircraft); owner->unit_type_counts=std::move(unit_types);
        owner->placed_unit_counts=std::move(placed_units); owner->unit_count=unit_count;
        factories.house_factory_counts[id]=counts;
        factories.power_blackout_idle.try_emplace(id,true);
        factories.power_dirty[id]=true;
    }
    return {Status::Done,{},{},{}};
}

void decrement(std::optional<std::map<std::int32_t,std::int32_t>>& counts,std::int32_t index) {
    if (!counts) return;
    const auto item=counts->find(index);
    if (item!=counts->end() && item->second>0) --item->second;
}

Outcome abandon_orphans(w::Snapshot& world,FactorySet& factories,const Catalog& catalog) {
    for (auto& factory:factories.factories) {
        if (!factory.object) continue;
        const auto wanted=product_kind(factory.slot);
        if (!wanted) return fail(Status::NeedsInput,"factory slot is unknown");
        if (choose_factory(world,factories,catalog,factory.house_id,*wanted,factory.slot==3)) continue;
        auto* item=entity(world,factory.object);
        auto* owner=house(world,factory.house_id);
        const auto* type=catalog.find(factory.object_type);
        if (!item || !owner || !type || item->house_id!=factory.house_id || item->type!=factory.object_type)
            return fail(Status::NeedsInput,"orphaned factory product is invalid");
        if (item->lifetime==w::Lifetime::Created) {
            // Prototype refund follows the older Factory::Abandon shape. The
            // exact base-game cost and destruction callbacks remain unresolved.
            if (owner->cash && type->cost>=0 && factory.balance>=0) {
                const auto paid=std::max<std::int64_t>(0,std::int64_t(type->cost)-factory.balance);
                *owner->cash=static_cast<std::int32_t>(std::min<std::int64_t>(
                    std::numeric_limits<std::int32_t>::max(),std::int64_t(*owner->cash)+paid));
            }
            switch (item->kind) {
            case w::Kind::Structure:
                decrement(owner->buildings_owned_5420,type->heap_index);
                factories.buildings.erase(item->id);
                break;
            case w::Kind::Unit:
                decrement(owner->units_owned_5430,type->heap_index);
                decrement(owner->unit_type_counts,type->heap_index);
                if (owner->unit_count && *owner->unit_count>0) --*owner->unit_count;
                break;
            case w::Kind::Infantry: decrement(owner->infantry_owned_5440,type->heap_index); break;
            case w::Kind::Aircraft: decrement(owner->aircraft_owned_5450,type->heap_index); break;
            default: return fail(Status::NeedsInput,"factory product kind is invalid");
            }
            item->lifetime=w::Lifetime::Destroyed; item->resolved_hp=0;
            item->active=false; item->in_limbo=true; item->on_map=false; item->next=0;
            item->object_field_74=0; item->object_field_76=1; item->target_id=0;
        }
        factory.object=0; factory.object_type.clear(); factory.progress=0;
        factory.interval=0; factory.balance=0; factory.suspended=true;
    }
    return {Status::Done,{},{},{}};
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

bool place_exit_actor(w::Snapshot& world,w::Entity& item,ra2map::Cell cell) {
    const auto found=world.cells.find(std::uint32_t(cell.y)*512+cell.x);
    if (found==world.cells.end() || !found->second.heads || !found->second.ground_occupancy_124 ||
        !found->second.terrain || !found->second.terrain->floor_world_height) return false;
    auto& target=found->second;
    if (target.heads->ground_head || *target.ground_occupancy_124) return false;
    w::Undo::save_cell(world,found->first);
    item.cell=cell; item.layer=w::Layer::Ground; item.lifetime=w::Lifetime::Placed;
    item.position=sim::Coord{int(cell.x)*256+128,int(cell.y)*256+128,*target.terrain->floor_world_height};
    item.next=target.heads->ground_head;
    target.heads->ground_head=item.id;
    // TODO-ORIGINAL: Infantry uses real subcell bits. Prototype movement
    // currently reserves bit 20 for both movable kinds.
    *target.ground_occupancy_124|=0x20;
    return activate(world,item);
}

// validate() held just before Stage::create_unit made this Unit. Since
// then only these things changed: the random state, one new Entity with
// one creation-order entry, House counters, the Unit's own fields, and one
// Cell whose empty ground head now names the Unit. validate() reads none
// of the other changes. So when the Unit and that Cell pass the checks
// below, the full validate() of the world returns true.
bool checked_new_unit(const w::Snapshot& world,std::uint32_t id,ra2map::Cell at) {
    try { (void)sim::Random(world.random); } catch (...) { return false; }
    if (!id || world.creation_order.empty() || world.creation_order.back().kind!=w::CreatedKind::Entity ||
        world.creation_order.back().id!=id) return false;
    const w::Entity* unit=nullptr;
    for (const auto& item:world.entities) if (item.id==id) { if (unit) return false; unit=&item; }
    for (const auto& item:world.houses) if (item.id==id) return false;
    if (!unit || unit->type.empty() || unit->kind!=w::Kind::Unit || unit->lifetime!=w::Lifetime::Placed ||
        !unit->position || !unit->cell || unit->layer!=w::Layer::Ground || unit->next!=std::optional<std::uint32_t>(0) ||
        unit->cell->x!=at.x || unit->cell->y!=at.y) return false;
    bool owner=false;
    for (const auto& item:world.houses) if (item.id==unit->house_id) owner=true;
    if (!owner) return false;
    const auto cell=world.cells.find(std::uint32_t(at.y)*512+at.x);
    return cell!=world.cells.end() && cell->second.heads && cell->second.heads->ground_head==id;
}

// valid: set when validate() is proved to hold for the world on return.
Outcome add_free_unit(w::Snapshot& world,const Catalog& catalog,const w::Entity& building,
                      const TypeFacts& building_type,bool& valid) {
    valid=false;
    if (building_type.free_unit.empty()) return {Status::Done,{},{},{}};
    const auto* type=catalog.find(building_type.free_unit);
    if (!type || type->rtti!=Rtti::UnitType || type->strength<=0)
        return fail(Status::NeedsInput,"free Unit type is unknown");
    const auto cell=exit_cell(world,catalog,building);
    if (!cell) return fail(Status::Rejected,"no clear exit for the free Unit");
    const auto owner_id=building.house_id;
    // TODO-ORIGINAL: replace this immediate exit with the refinery's original
    // free-unit creation, docking, animation, and radio phases.
    // Create the Unit in place, with the checks of a Stage built from a
    // copy of this world. The transaction Undo covers the edit.
    if (world.edition!=w::Edition::Base || !w::validate(world)) throw std::invalid_argument("invalid base staged world");
    const auto id=w::create_staged_unit(world,owner_id,type->id,type->strength,type->heap_index,false);
    if (!id) return fail(Status::NeedsInput,"free Unit construction is unresolved");
    auto* item=entity(world,*id);
    auto* owner=house(world,owner_id);
    if (!item || !owner || !owner->units_owned_5430 || !owner->units_ever_54b0 ||
        !owner->placed_unit_counts) return fail(Status::NeedsInput,"free Unit House counts are unknown");
    item->maximum_hp=type->strength;
    for (auto* counts:{&*owner->units_owned_5430,&*owner->units_ever_54b0,&*owner->placed_unit_counts}) {
        auto& count=(*counts)[type->heap_index];
        if (count==std::numeric_limits<std::int32_t>::max())
            return fail(Status::NeedsInput,"free Unit count is too large");
        ++count;
    }
    if (!place_exit_actor(world,*item,*cell)) return fail(Status::NeedsInput,"free Unit exit is unresolved");
    valid=checked_new_unit(world,*id,*cell);
    return {Status::Done,{},*id,{}};
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
        if (!place_exit_actor(world,*item,*cell)) return fail(Status::NeedsInput,"factory object cannot be activated");
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
        const auto* owner=house(candidate,item.house_id);
        if (!owner || owner->role!=w::HouseRole::Player || !catalog.find(item.type)) continue;
        next.buildings.try_emplace(item.id,BuildingState{});
    }
    {
        // Rebuild the Cell map in key order. The content is the same; the
        // nodes now sit in memory in the order that full Cell walks read them.
        // Check after the rebuild, so the check's saved node addresses point
        // at the final map for the first match frame.
        std::map<std::uint32_t,w::CellState> cells;
        for (auto& item:candidate.cells) cells.emplace_hint(cells.end(),item.first,std::move(item.second));
        candidate.cells=std::move(cells);
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
    bool known_valid=false;
    if (ready(world)) {
        // Find a failure first without the transaction copy. These checks
        // match the transaction below up to its first write, and that
        // transaction would roll back such a failure exactly.
        struct Progress {
            w::Snapshot& world; w::SetupProgress saved;
            ~Progress() { world.setup_progress=saved; }
        } progress{world,world.setup_progress};
        world.setup_progress=w::SetupProgress::Complete;
        const auto proximity=ra2::sim::production::passes_proximity(world,factories,catalog,house_id,type,cell);
        if (!proximity) return fail(Status::NeedsInput,"building proximity is unknown");
        if (!*proximity) return fail(Status::Rejected,"building is too far from the base");
        if (auto failure=ra2::sim::production::place_failure(world,factories,catalog,house_id,type,cell)) return *failure;
    }
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        const auto proximity=ra2::sim::production::passes_proximity(candidate,next,catalog,house_id,type,cell);
        if (!proximity) return fail(Status::NeedsInput,"building proximity is unknown");
        if (!*proximity) return fail(Status::Rejected,"building is too far from the base");
        auto result=ra2::sim::production::place(candidate,next,catalog,house_id,type,cell,frame);
        if (result.status==Status::Done && result.entity) {
            auto* building=entity(candidate,*result.entity);
            if (!building || !activate(candidate,*building)) return fail(Status::NeedsInput,"building is incomplete");
            const auto* building_type=catalog.find(building->type);
            if (!building_type) return fail(Status::NeedsInput,"building type is missing");
            const auto free_unit=add_free_unit(candidate,catalog,*building,*building_type,known_valid);
            if (free_unit.status!=Status::Done) return free_unit;
        }
        return result;
    },&known_valid);
}

Outcome tick(w::Snapshot& world,FactorySet& factories,const Catalog& catalog,std::uint64_t frame) {
    loop::profile::Scope probe(loop::profile::Phase::Production);
    if (!ready(world)) return fail(Status::NeedsInput,"prototype setup is not complete");
    bool needed=false,waiting_for_exit=false;
    for (const auto& item:factories.power_dirty) if (item.second) { needed=true; break; }
    for (const auto& tracked:factories.buildings) {
        const auto* building=entity(world,tracked.first);
        if (!building) return fail(Status::NeedsInput,"tracked building is missing");
        if (building->lifetime==w::Lifetime::Destroyed) { needed=true; break; }
    }
    for (const auto& factory:factories.factories) {
        if (!factory.object) continue;
        const auto wanted=product_kind(factory.slot);
        if (!wanted) return fail(Status::NeedsInput,"factory slot is unknown");
        if (!choose_factory(world,factories,catalog,factory.house_id,*wanted,factory.slot==3)) needed=true;
        if (factory.progress==54 && (factory.slot==1 || factory.slot==2)) {
            const auto* item=entity(world,factory.object);
            const auto expected=factory.slot==1 ? w::Kind::Infantry : w::Kind::Unit;
            if (!item || item->kind!=expected || item->lifetime!=w::Lifetime::Created ||
                item->house_id!=factory.house_id) return fail(Status::NeedsInput,"completed factory object is invalid");
            const auto* building=choose_factory(world,factories,catalog,factory.house_id,*wanted);
            if (building && exit_cell(world,catalog,*building)) needed=true;
            else waiting_for_exit=true;
        }
        if (!factory.suspended && factory.progress<54 &&
            (frame<factory.timer_start || frame-factory.timer_start>=std::uint64_t(std::max(factory.interval,0)))) needed=true;
    }
    if (!needed) return {Status::Done,waiting_for_exit ? "waiting for a clear factory exit" : "",{}, {}};
    return transaction(world,factories,[&](w::Snapshot& candidate,FactorySet& next) {
        auto cleanup=cleanup_destroyed(candidate,next,catalog);
        if (cleanup.status!=Status::Done) return cleanup;
        cleanup=abandon_orphans(candidate,next,catalog);
        if (cleanup.status!=Status::Done) return cleanup;
        auto result=ra2::sim::production::tick(candidate,next,catalog,frame);
        if (result.status!=Status::Done) return result;
        return discharge(candidate,next,catalog);
    });
}
}
