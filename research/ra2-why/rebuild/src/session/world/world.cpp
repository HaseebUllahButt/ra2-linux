#include "world.h"
#include <map>
#include <limits>
#include <set>
#include <stdexcept>

namespace ra2::session::world {
std::optional<bool> base_unit_on_map(const PlacementTerrain& terrain, StartPoint point) {
    // Base 0055A660 uses the same local-boundary arithmetic as Yuri 00578460.
    return terrain.in_map(point,PlacementEdition::Yuri);
}
Stage::Stage(const sim::RandomState& random) {
    (void)sim::Random(random);
    snapshot_.edition=Edition::Base;
    snapshot_.random=random;
}
Stage::Stage(Snapshot snapshot) {
    if (snapshot.edition!=Edition::Base || !validate(snapshot))
        throw std::invalid_argument("invalid base staged world");
    snapshot_=std::move(snapshot);
}
std::optional<std::uint32_t> Stage::next_id() const {
    std::uint32_t largest=0;
    for (const auto& item:snapshot_.creation_order) if (item.id>largest) largest=item.id;
    if (largest==std::numeric_limits<std::uint32_t>::max()) return {};
    return largest+1;
}
std::optional<std::uint32_t> Stage::create_player_house(std::string name, std::string country,
    std::int32_t credits, std::int32_t selected_color, std::int32_t unit_registry_size,
    std::optional<std::int32_t> original_index) {
    if (name.empty() || country.empty() || credits<0 || selected_color<0 || selected_color>7 ||
        unit_registry_size<0 || unit_registry_size>512 || (original_index && *original_index<0)) return {};
    for (const auto& existing:snapshot_.houses)
        if (existing.role && *existing.role!=HouseRole::Player) return {};
    const auto id=next_id(); if (!id) return {};
    auto random=sim::Random(snapshot_.random);
    // Base House constructor 004E25F6 draws this value before Unit creation.
    const auto constructor_random=random.ranged(450,1800);
    // Base 004E8300 copies credits and selected color; 006715D0 maps the
    // house display color later in the multiplayer constructor.
    const auto fields=initialize_house_values(credits,selected_color);
    House house;
    house.id=*id; house.original_index=original_index;
    house.role=HouseRole::Player;
    house.name=std::move(name); house.country=std::move(country);
    house.initial_credits=fields.initial_credits; house.cash=fields.cash;
    house.selected_color=fields.color; house.palette_color=multiplayer_house_color(selected_color);
    house.constructor_random=constructor_random; house.unit_registry_size=unit_registry_size;
    // 004E1BE3 and 004E1C33 initialize the Unit counters to zero.
    house.unit_count=0; house.unit_type_counts.emplace(); house.unit_fixed_counts.emplace();
    house.placed_unit_counts.emplace();
    snapshot_.houses.reserve(snapshot_.houses.size()+1);
    snapshot_.creation_order.reserve(snapshot_.creation_order.size()+1);
    snapshot_.houses.push_back(std::move(house));
    snapshot_.creation_order.push_back({CreatedKind::House,*id});
    snapshot_.random=random.state();
    return id;
}
std::optional<std::uint32_t> Stage::create_auxiliary_house(HouseRole role,
    std::int32_t unit_registry_size, std::optional<std::int32_t> resolved_palette_color,
    std::optional<std::int32_t> original_index) {
    if ((role!=HouseRole::Neutral && role!=HouseRole::Special) || unit_registry_size<0 ||
        unit_registry_size>512 || (resolved_palette_color && *resolved_palette_color<0) ||
        (original_index && *original_index<0)) return {};
    bool have_player=false,have_neutral=false,have_special=false;
    for (const auto& existing:snapshot_.houses) if (existing.role) {
        have_player|=*existing.role==HouseRole::Player;
        have_neutral|=*existing.role==HouseRole::Neutral;
        have_special|=*existing.role==HouseRole::Special;
    }
    if (!have_player || have_special || (role==HouseRole::Neutral && have_neutral) ||
        (role==HouseRole::Special && !have_neutral)) return {};
    const auto id=next_id(); if (!id) return {};
    auto random=sim::Random(snapshot_.random);
    House house;
    house.id=*id; house.original_index=original_index; house.role=role;
    house.name=role==HouseRole::Neutral ? "Neutral" : "Special";
    house.country=house.name; house.palette_color=resolved_palette_color;
    house.constructor_random=random.ranged(450,1800);
    house.unit_registry_size=unit_registry_size;
    house.unit_count=0; house.unit_type_counts.emplace(); house.unit_fixed_counts.emplace();
    house.placed_unit_counts.emplace();
    snapshot_.houses.reserve(snapshot_.houses.size()+1);
    snapshot_.creation_order.reserve(snapshot_.creation_order.size()+1);
    snapshot_.houses.push_back(std::move(house));
    snapshot_.creation_order.push_back({CreatedKind::House,*id});
    snapshot_.random=random.state();
    return id;
}
std::optional<std::uint32_t> Stage::create_unit(std::uint32_t house_id, std::string type,
    std::optional<std::int32_t> resolved_hp, std::int32_t original_type_index,
    bool excluded_from_house_tracking) {
    auto found=snapshot_.houses.end();
    for (auto it=snapshot_.houses.begin();it!=snapshot_.houses.end();++it)
        if (it->id==house_id) { found=it; break; }
    if (found==snapshot_.houses.end() || type.empty() || (resolved_hp && *resolved_hp<=0) ||
        original_type_index<0 || original_type_index>=512 || !found->unit_registry_size ||
        original_type_index>=*found->unit_registry_size) return {};
    const auto id=next_id(); if (!id) return {};
    // Unit 006FA400 calls its Techno parent. At 006C12AA the parent consumes
    // one shared RNG word and stores its low 16 bits at Unit +320.
    auto random=sim::Random(snapshot_.random);
    const auto constructor_random=static_cast<std::uint16_t>(random.next());
    Entity unit;
    unit.id=*id; unit.house_id=house_id; unit.type=std::move(type);
    unit.kind=Kind::Unit; unit.lifetime=Lifetime::Created; unit.resolved_hp=resolved_hp;
    unit.constructor_random=constructor_random;
    unit.next=0; unit.on_map=false; unit.hull_facing=0; unit.turret_facing=0;
    unit.occupancy_tracking=true; // Foot constructor 004C2B6E.
    unit.original_type_index=original_type_index;
    unit.excluded_from_house_tracking=excluded_from_house_tracking;
    auto updated_house=*found;
    // 004EA970 updates the Unit count and two type registries unless the
    // original UnitType +202 flag excludes this object from tracking.
    if (!excluded_from_house_tracking) {
        if (!updated_house.unit_count || !updated_house.unit_type_counts || !updated_house.unit_fixed_counts ||
            *updated_house.unit_count==std::numeric_limits<std::int32_t>::max()) return {};
        auto& dynamic=(*updated_house.unit_type_counts)[original_type_index];
        auto& fixed=(*updated_house.unit_fixed_counts)[original_type_index];
        if (dynamic==std::numeric_limits<std::int32_t>::max() || fixed==std::numeric_limits<std::int32_t>::max()) return {};
        ++*updated_house.unit_count; ++dynamic; ++fixed;
    }
    snapshot_.entities.reserve(snapshot_.entities.size()+1);
    snapshot_.creation_order.reserve(snapshot_.creation_order.size()+1);
    snapshot_.entities.push_back(std::move(unit));
    snapshot_.creation_order.push_back({CreatedKind::Entity,*id});
    *found=std::move(updated_house);
    snapshot_.random=random.state();
    return id;
}
bool Stage::install_base_cells(PlacementGeometry geometry, std::map<std::uint32_t,CellState> cells,
    std::optional<std::int32_t> tube_count, std::optional<std::int32_t> session_field) {
    if (snapshot_.geometry || cells.empty() || (tube_count && *tube_count<0) ||
        !placement_cell_in_map({0,0},PlacementEdition::Base,geometry,std::nullopt)) return false;
    auto candidate=snapshot_;
    candidate.geometry=geometry; candidate.cells=std::move(cells);
    candidate.tube_vector_count=tube_count; candidate.session_field_838=session_field;
    if (!validate(candidate)) return false;
    snapshot_=std::move(candidate);
    return true;
}
PlacementResult Stage::place_base_unit(std::uint32_t unit_id, StartPoint point,
    std::uint8_t facing, const BaseUnitProfile& profile) {
    // This slice is the ordinary, unlinked, single-cell ground Unit path.
    // 00703040 and 004C8CA0 have many additional branches.
    if (snapshot_.pending_cell_setup || !snapshot_.geometry || point.x<1 || point.y<1 || point.x>510 || point.y>510 ||
        !profile.restricted_land || *profile.restricted_land!=-1 ||
        !profile.movement_mode || *profile.movement_mode!=-1 ||
        !profile.placement_mode || *profile.placement_mode!=2 ||
        !profile.speed_type || *profile.speed_type!=1 ||
        !profile.level_height || *profile.level_height<=0 ||
        !profile.type_flag_aa0 || *profile.type_flag_aa0 || !profile.unit_flag_32c || *profile.unit_flag_32c ||
        !profile.linked_team || *profile.linked_team || !profile.type_flag_205 || !*profile.type_flag_205 ||
        !profile.unit_flag_32a || *profile.unit_flag_32a || !profile.type_flag_bb0 || *profile.type_flag_bb0 ||
        !profile.type_flag_bb1 || *profile.type_flag_bb1 || !snapshot_.tube_vector_count ||
        *snapshot_.tube_vector_count!=0) return PlacementResult::Unresolved;
    auto candidate=snapshot_;
    auto unit=candidate.entities.end();
    for (auto it=candidate.entities.begin();it!=candidate.entities.end();++it)
        if (it->id==unit_id) { unit=it; break; }
    if (unit==candidate.entities.end() || unit->kind!=Kind::Unit || unit->lifetime!=Lifetime::Created ||
        !unit->resolved_hp || !unit->original_type_index || !unit->next || *unit->next!=0 ||
        !unit->occupancy_tracking || !*unit->occupancy_tracking)
        return PlacementResult::Unresolved;
    auto house=candidate.houses.end();
    for (auto it=candidate.houses.begin();it!=candidate.houses.end();++it)
        if (it->id==unit->house_id) { house=it; break; }
    if (house==candidate.houses.end() || !house->placed_unit_counts) return PlacementResult::Unresolved;
    const auto key=std::uint32_t(point.y)*512+std::uint32_t(point.x);
    auto cell=candidate.cells.find(key);
    // Unit entry 00703087 reads Cell +124. Its bridge path at 00703281
    // reads Cell +140 bit 100. Unknown runtime state cannot mean zero.
    if (cell==candidate.cells.end() || !cell->second.terrain || !cell->second.heads ||
        !cell->second.unit_land_speed || !cell->second.coarse_category ||
        !cell->second.usable || !*cell->second.usable ||
        !cell->second.ground_occupancy_124 || *cell->second.ground_occupancy_124!=0 ||
        !cell->second.cell_flag_100 || *cell->second.cell_flag_100 ||
        !cell->second.cell_field_54 || *cell->second.cell_field_54!=-1 ||
        !cell->second.cell_field_58 || *cell->second.cell_field_58!=-1 ||
        !cell->second.tube_field_116 || *cell->second.tube_field_116!=-1)
        return PlacementResult::Unresolved;
    const auto& terrain=*cell->second.terrain;
    if (!terrain.height_level || (*terrain.height_level!=0 && *terrain.height_level!=4) ||
        !terrain.ramp_index || *terrain.ramp_index!=0 ||
        !terrain.bridge || *terrain.bridge || !terrain.overlay_type_index || *terrain.overlay_type_index!=-1 ||
        !terrain.land_type || (*terrain.land_type!=0 && *terrain.land_type!=2) ||
        !terrain.floor_world_height ||
        std::int64_t(*terrain.floor_world_height)!=std::int64_t(*terrain.height_level) * *profile.level_height ||
        cell->second.heads->ground_head || cell->second.heads->bridge_head ||
        (*cell->second.unit_land_speed!=0.0f && *cell->second.unit_land_speed!=1.0f))
        return PlacementResult::Unresolved;
    const auto category=coarse_inputs(candidate,key);
    if (!category) return PlacementResult::Unresolved;
    try {
        if (sim::original_coarse_category(*category).category!=*cell->second.coarse_category)
            return PlacementResult::Unresolved;
    } catch (const std::invalid_argument&) { return PlacementResult::Unresolved; }
    const auto on_map=placement_cell_in_map(point,PlacementEdition::Yuri,*candidate.geometry,
        PlacementCell{static_cast<std::int8_t>(*terrain.height_level),0});
    if (!on_map || !*on_map) return PlacementResult::Unresolved;
    unit->on_map=true; unit->base_profile=profile;
    // Base 006C4610 sets +32D before 005D3890 checks the Unit cell entry.
    if (*cell->second.unit_land_speed==0.0f) {
        snapshot_=std::move(candidate);
        return PlacementResult::Blocked;
    }
    static constexpr int dx[8]={0,1,1,1,0,-1,-1,-1};
    static constexpr int dy[8]={-1,-1,0,1,1,1,0,-1};
    for (int i=0;i<8;++i) {
        const auto neighbor=std::uint32_t(int(point.y)+dy[i])*512+std::uint32_t(int(point.x)+dx[i]);
        auto found=candidate.cells.find(neighbor);
        if (found==candidate.cells.end() || !found->second.neighbor_count) return PlacementResult::Unresolved;
    }
    auto count=house->placed_unit_counts->find(*unit->original_type_index);
    if (count!=house->placed_unit_counts->end() && count->second==std::numeric_limits<std::int32_t>::max())
        return PlacementResult::Unresolved;
    // Original 0054AB70 uses a single (0,0) footprint from 005D5D90.
    weapons::ContentLinks links;
    links.cells.emplace(key,*cell->second.heads);
    links.objects.emplace(unit->id,weapons::ContentObject{0,1});
    if (links.place({key},weapons::ContentLayer::Ground,unit->id,true)!=weapons::ContentResult::Complete)
        return PlacementResult::Unresolved;
    cell->second.heads=links.cells.at(key);
    unit->next=links.objects.at(unit->id).next;
    // Unit virtual +E8 at 00707980 sets the ground occupancy bit. The
    // restricted path has a clear bridge flag and an initially empty byte.
    *cell->second.ground_occupancy_124|=0x20;
    ++(*house->placed_unit_counts)[*unit->original_type_index]; // 004ED70C.
    for (int i=0;i<8;++i) {
        const auto neighbor=std::uint32_t(int(point.y)+dy[i])*512+std::uint32_t(int(point.x)+dx[i]);
        auto& value=*candidate.cells.at(neighbor).neighbor_count;
        value=static_cast<std::uint8_t>(value+1); // Foot 004C5F6A, byte wrap.
    }
    unit->cell=ra2map::Cell{static_cast<std::uint16_t>(point.x),static_cast<std::uint16_t>(point.y)};
    unit->position=sim::Coord{int(point.x)*256+128,int(point.y)*256+128,*terrain.floor_world_height};
    unit->layer=Layer::Ground; unit->lifetime=Lifetime::Placed;
    unit->hull_facing=facing; unit->turret_facing=facing;
    unit->timer_length=0; unit->timer_active=false; // Unit 006FC9AB.
    // 006C47B9 divides world Z by LevelHeight and stores the low byte times 10.
    unit->height_byte=static_cast<std::uint8_t>((*terrain.floor_world_height / *profile.level_height)*10);
    // 0054AB70 invokes Cell setup 00472540 after linking. The test oracle
    // supplies that body, so its resulting category and side effects remain
    // unknown. Require a resolved snapshot before another placement.
    cell->second.coarse_category.reset();
    candidate.pending_cell_setup=true;
    if (!validate(candidate)) return PlacementResult::Unresolved;
    snapshot_=std::move(candidate);
    return PlacementResult::Placed;
}
bool validate(const Snapshot& snapshot) {
    if (snapshot.edition!=Edition::Base && snapshot.edition!=Edition::Yuri) return false;
    try { (void)sim::Random(snapshot.random); } catch (...) { return false; }
    std::set<std::uint32_t> ids;
    std::map<std::uint32_t,const House*> houses;
    std::map<std::uint32_t,const Entity*> entities;
    for (const auto& house:snapshot.houses) {
        if (!house.id || !ids.insert(house.id).second || house.name.empty() || house.country.empty()) return false;
        if (house.role && *house.role!=HouseRole::Player && *house.role!=HouseRole::Neutral && *house.role!=HouseRole::Special) return false;
        houses.emplace(house.id,&house);
    }
    for (const auto& entity:snapshot.entities) {
        if (!entity.id || !ids.insert(entity.id).second || entity.type.empty() ||
            (!houses.count(entity.house_id) && !(entity.kind==Kind::Terrain && !entity.house_id))) return false;
        if (entity.kind!=Kind::Unit && entity.kind!=Kind::Infantry &&
            entity.kind!=Kind::Aircraft && entity.kind!=Kind::Structure && entity.kind!=Kind::Terrain) return false;
        if (entity.lifetime!=Lifetime::Created && entity.lifetime!=Lifetime::Placed &&
            entity.lifetime!=Lifetime::Destroyed) return false;
        if (entity.lifetime==Lifetime::Placed && (!entity.position || !entity.cell || !entity.layer)) return false;
        if (entity.layer && *entity.layer!=Layer::Ground && *entity.layer!=Layer::Bridge) return false;
        entities.emplace(entity.id,&entity);
    }
    if (snapshot.creation_order.size()!=ids.size()) return false;
    std::set<std::uint32_t> ordered;
    for (const auto& object:snapshot.creation_order) {
        if (!ordered.insert(object.id).second) return false;
        if (object.kind==CreatedKind::House) { if (!houses.count(object.id)) return false; }
        else if (object.kind==CreatedKind::Entity) { if (!entities.count(object.id)) return false; }
        else return false;
    }
    std::set<std::uint32_t> complete;
    for (const auto& item:entities) {
        const auto& entity=*item.second;
        if (entity.next && *entity.next && !entities.count(*entity.next)) return false;
        std::set<std::uint32_t> chain;
        auto current=entity.id;
        while (current && !complete.count(current)) {
            if (!chain.insert(current).second) return false;
            const auto it=entities.find(current);
            if (it==entities.end() || it->second->lifetime!=Lifetime::Placed) {
                if (current==entity.id && (!entity.next || !*entity.next) && entity.lifetime!=Lifetime::Placed) break;
                return false;
            }
            if (!it->second->next) break;
            current=*it->second->next;
        }
        complete.insert(chain.begin(),chain.end());
    }
    for (const auto& item:snapshot.cells) {
        if (item.first>=512*512) return false;
        if (item.second.terrain && (item.second.terrain->cell.x>=512 || item.second.terrain->cell.y>=512 ||
            std::uint32_t(item.second.terrain->cell.y)*512+item.second.terrain->cell.x!=item.first)) return false;
        if (item.second.heads)
            for (const auto head:{item.second.heads->ground_head,item.second.heads->bridge_head})
                if (head && (!entities.count(head) || entities.at(head)->lifetime!=Lifetime::Placed)) return false;
    }
    return true;
}
std::optional<sim::CoarseCategoryInputs> coarse_inputs(const Snapshot& snapshot, std::uint32_t cell) {
    const auto found=snapshot.cells.find(cell);
    if (found==snapshot.cells.end()) return {};
    sim::CoarseCategoryInputs input;
    const auto& state=found->second;
    input.usable=state.usable;
    input.overlay=state.overlay;
    input.land_track=state.land_track;
    input.session_field_1258=snapshot.session_field_838;
    if (state.terrain) {
        input.overlay_index=state.terrain->overlay_type_index;
        input.land_type=state.terrain->land_type;
    }
    std::map<std::uint32_t,const Entity*> objects;
    for (const auto& object:snapshot.entities)
        if (!object.id || !objects.emplace(object.id,&object).second) return {};
    if (!state.heads) return input;
    std::vector<sim::CoarseOccupantState> occupants;
    std::set<std::uint32_t> seen;
    auto current=state.heads->ground_head;
    while (current) {
        const auto item=objects.find(current);
        if (!seen.insert(current).second || item==objects.end() || item->second->lifetime!=Lifetime::Placed) return {};
        occupants.push_back(item->second->category.value_or(sim::CoarseOccupantState{}));
        if (!item->second->next) return input;
        current=*item->second->next;
    }
    input.occupants=std::move(occupants);
    return input;
}
}
