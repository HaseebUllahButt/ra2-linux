#include "world.h"
#include "sim/floor.h"
#include "world_hash.h"
#include "loop/profile.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
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
    if (snapshot_.pending_cell_setup || name.empty() || country.empty() || credits<0 || selected_color<0 || selected_color>7 ||
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
    if (snapshot_.pending_cell_setup || (role!=HouseRole::Neutral && role!=HouseRole::Special) || unit_registry_size<0 ||
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
    if (snapshot_.pending_cell_setup || found==snapshot_.houses.end() || type.empty() || (resolved_hp && *resolved_hp<=0) ||
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
    // Fresh Unit constructor values checked against the base executable.
    unit.object_field_74=0; unit.object_field_75=0; unit.object_field_76=1;
    unit.techno_field_370=0; unit.unit_field_7d=1; unit.techno_field_1b8=0; unit.foot_field_5a1=0;
    unit.veterancy=0.0; unit.firepower_multiplier=1.0;
    unit.occupancy_tracking=true; // Foot constructor 004C2B6E.
    unit.category.emplace();
    unit.category->runtime_type=1; // Unit virtual +2C, base 0070A2D0.
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
    if (snapshot_.pending_cell_setup || snapshot_.geometry || cells.empty() || (tube_count && *tube_count<0) ||
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
    if (point.x<0 || point.y<0 || point.x>=512 || point.y>=512) return PlacementResult::Unresolved;
    const auto found=snapshot_.cells.find(std::uint32_t(point.y)*512+point.x);
    if (found==snapshot_.cells.end() || !found->second.terrain || !found->second.terrain->floor_world_height)
        return PlacementResult::Unresolved;
    return place_base_unit_at(unit_id,point,*found->second.terrain->floor_world_height,facing,profile);
}
PlacementResult Stage::place_base_unit_at(std::uint32_t unit_id, StartPoint point,
    std::int32_t world_z, std::uint8_t facing, const BaseUnitProfile& profile) {
    // This slice is the ordinary, unlinked, single-cell ground Unit path.
    // 00703040 and 004C8CA0 have many additional branches.
    if (snapshot_.pending_cell_setup || !snapshot_.geometry || point.x<1 || point.y<1 || point.x>510 || point.y>510 ||
        !profile.restricted_land || *profile.restricted_land!=-1 ||
        !profile.movement_mode || *profile.movement_mode!=-1 ||
        !profile.placement_mode || *profile.placement_mode!=2 ||
        !profile.speed_type || *profile.speed_type!=1 ||
        !profile.level_height || *profile.level_height!=sim::original_level_height ||
        !profile.leptons_per_sight_increase || *profile.leptons_per_sight_increase<=0 ||
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
        !unit->occupancy_tracking || !*unit->occupancy_tracking ||
        unit->object_field_74!=0 || unit->object_field_75!=0 || unit->object_field_76!=1 ||
        unit->techno_field_370!=0 || unit->unit_field_7d!=1 || unit->techno_field_1b8!=0 ||
        unit->foot_field_5a1!=0)
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
        (world_z!=0 && world_z!=*terrain.floor_world_height) ||
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
    unit->object_field_74=1; unit->object_field_75=1; unit->object_field_76=0;
    unit->cell=ra2map::Cell{static_cast<std::uint16_t>(point.x),static_cast<std::uint16_t>(point.y)};
    unit->position=sim::Coord{int(point.x)*256+128,int(point.y)*256+128,world_z};
    unit->layer=Layer::Ground; unit->lifetime=Lifetime::Placed;
    unit->hull_facing=facing; unit->turret_facing=facing;
    unit->timer_length=0; unit->timer_active=false; // Unit 006FC9AB.
    // 006C47B9 divides world Z by Rules +1324, then stores the low byte times 10.
    const auto height_units=std::int64_t(world_z) / *profile.leptons_per_sight_increase;
    const auto height_low=static_cast<std::uint8_t>(height_units);
    unit->height_byte=static_cast<std::uint8_t>(unsigned(height_low)*10u);
    // 0054AB70 invokes Cell setup 00472540 after linking. The test oracle
    // supplies that body, so its resulting category and side effects remain
    // unknown. Require a resolved snapshot before another placement.
    cell->second.coarse_category.reset();
    // Cell setup can write +140 bits; do not retain a stale full word.
    cell->second.cell_flags_140.reset();
    cell->second.cell_flag_80.reset();
    cell->second.coarse_category_writes.reset();
    cell->second.coarse_cache_height.reset();
    cell->second.hierarchy_cache_height.reset();
    candidate.pending_cell_setup=true;
    candidate.pending_setup_cell=key;
    if (!validate(candidate)) return PlacementResult::Unresolved;
    snapshot_=std::move(candidate);
    return PlacementResult::Placed;
}
sim::CellSetupResult Stage::complete_pending_cell_setup(const sim::CellSetupStatic& guard,
    const sim::TheaterInputs& theater, std::uint8_t cliff,
    const std::map<std::int32_t,float>& land_wheels) {
    sim::CellSetupResult unresolved;
    unresolved.detail="pending allocated Cell setup is unresolved";
    if (!snapshot_.pending_cell_setup || !snapshot_.pending_setup_cell) return unresolved;
    const auto key=*snapshot_.pending_setup_cell;
    const auto found=snapshot_.cells.find(key);
    if (found==snapshot_.cells.end() || !found->second.terrain) return unresolved;

    // Allocation is supplied independently from terrain and map membership.
    sim::CellSetupState input;
    input.allocated=found->second.allocated;
    input.terrain=*found->second.terrain;
    input.field_11d=found->second.cell_field_11d;
    input.animated_20000=found->second.cell_flag_20000;
    auto candidate=snapshot_;
    candidate.pending_cell_setup=false;
    candidate.pending_setup_cell.reset();
    sim::CellSetupCallbacks callbacks;
    callbacks.neighbor=[&](ra2map::Cell coord)->std::optional<sim::CellSetupNeighbor> {
        const auto index=std::uint32_t(coord.y)*512+coord.x;
        const auto neighbor=candidate.cells.find(index);
        if (neighbor==candidate.cells.end() || !neighbor->second.allocated) return {};
        if (!*neighbor->second.allocated) return sim::CellSetupNeighbor{};
        if (!neighbor->second.terrain) return {};
        return sim::CellSetupNeighbor{true,*neighbor->second.terrain};
    };
    callbacks.category=[&](const sim::TerrainCell& terrain)->std::optional<sim::CoarseCategoryInputs> {
        auto profile=coarse_inputs(candidate,key);
        if (!profile) return {};
        profile->overlay_index=terrain.overlay_type_index;
        profile->land_type=terrain.land_type;
        profile->land_wheel.reset();
        if (terrain.land_type) {
            const auto wheel=land_wheels.find(*terrain.land_type);
            if (wheel!=land_wheels.end()) profile->land_wheel=wheel->second;
        }
        return profile;
    };
    auto result=sim::original_overlay_free_cell_setup(input,guard,theater,cliff,callbacks);
    if (result.status!=sim::CellSetupStatus::Complete) return result;
    if (!result.category || !result.cache_height) {
        unresolved.detail="complete Cell setup omitted category or cache height";
        return unresolved;
    }
    auto& cell=candidate.cells.at(key);
    // Unit locomotor speed and coarse-category Wheel are separate lookups.
    // A changed or unknown LandType invalidates the former, not the latter.
    if (!cell.terrain->land_type || !result.state.terrain.land_type ||
        *cell.terrain->land_type!=*result.state.terrain.land_type)
        cell.unit_land_speed.reset();
    cell.terrain=result.state.terrain;
    cell.cell_field_11d=result.state.field_11d;
    cell.cell_flag_20000=result.state.animated_20000;
    cell.coarse_category=result.category->category;
    cell.coarse_category_writes=result.category->writes;
    cell.coarse_cache_height=*result.cache_height;
    cell.hierarchy_cache_height=*result.cache_height;
    cell.land_wheel.reset();
    if (cell.terrain->land_type) {
        const auto wheel=land_wheels.find(*cell.terrain->land_type);
        if (wheel!=land_wheels.end()) cell.land_wheel=wheel->second;
    }
    if (!validate(candidate)) {
        unresolved.detail="Cell setup produced an invalid snapshot";
        return unresolved;
    }
    snapshot_=std::move(candidate);
    return result;
}

bool Stage::finish_battle_house_updates(std::uint32_t house_id) {
    if (snapshot_.pending_cell_setup) return false;
    auto candidate=snapshot_;
    for (auto& house:candidate.houses) if (house.id==house_id) {
        if (!house.buildings_ever_54a0 || !house.aircraft_ever_54d0 ||
            !house.infantry_ever_54c0 || !house.units_ever_54b0) return false;
        house.buildings_ever_54a0->clear(); house.aircraft_ever_54d0->clear();
        house.infantry_ever_54c0->clear(); house.units_ever_54b0->clear();
        snapshot_=std::move(candidate);
        return true;
    }
    return false;
}


bool Stage::finish_battle_house(std::uint32_t house_id,std::int32_t remaining_budget) {
    if (snapshot_.pending_cell_setup) return false;
    auto candidate=snapshot_;
    bool found=false;
    for (auto& house:candidate.houses) if (house.id==house_id) {
        if (!house.cash) return false;
        if (remaining_budget>0) {
            const auto cash=std::int64_t(*house.cash)+remaining_budget;
            if (cash>std::numeric_limits<std::int32_t>::max()) return false;
            house.cash=static_cast<std::int32_t>(cash);
        }
        found=true;
        break;
    }
    if (!found) return false;
    Stage working(std::move(candidate));
    if (!working.finish_battle_house_updates(house_id)) return false;
    snapshot_=working.snapshot();
    return true;
}

namespace {
thread_local Undo* active_undo=nullptr;
bool undo_check() {
    static const bool enabled=[]{ const char* v=std::getenv("RA2_UNDO_CHECK"); return v && *v && *v!='0'; }();
    return enabled;
}
}
Undo::Undo(Snapshot& snapshot):snapshot_(snapshot),outer_(active_undo) {
    auto cells=std::move(snapshot_.cells);
    try { backup_=snapshot_; }
    catch (...) { snapshot_.cells=std::move(cells); throw; }
    snapshot_.cells=std::move(cells);
    if (undo_check()) check_=snapshot_;
    active_undo=this;
}
Undo::~Undo() {
    active_undo=outer_;
    if (!committed_) restore();
}
void Undo::restore() {
    for (auto it=cells_.rbegin();it!=cells_.rend();++it) snapshot_.cells[it->first]=std::move(it->second);
    backup_.cells=std::move(snapshot_.cells);
    snapshot_=std::move(backup_);
    if (check_ && world_hash(snapshot_)!=world_hash(*check_)) {
        std::fprintf(stderr,"RA2_UNDO_CHECK: restored world differs from its full copy\n");
        std::abort();
    }
}
void Undo::save_cell(const Snapshot& snapshot, std::uint32_t key) {
    for (auto* undo=active_undo;undo;undo=undo->outer_) {
        if (&undo->snapshot_!=&snapshot) continue; // An Undo restores only its own Snapshot.
        if (std::any_of(undo->cells_.begin(),undo->cells_.end(),[&](const auto& item){return item.first==key;})) continue;
        const auto found=snapshot.cells.find(key);
        if (found!=snapshot.cells.end()) undo->cells_.emplace_back(key,found->second);
    }
}
bool validate(const Snapshot& snapshot) {
    loop::profile::Timer probe(loop::profile::Phase::Validation);
    if (snapshot.edition!=Edition::Base && snapshot.edition!=Edition::Yuri) return false;
    if (snapshot.setup_progress!=SetupProgress::Unknown && snapshot.setup_progress!=SetupProgress::PackedMapRead &&
        snapshot.setup_progress!=SetupProgress::Complete && snapshot.setup_progress!=SetupProgress::OverlayObjectsRead &&
        snapshot.setup_progress!=SetupProgress::OverlayDataRead &&
        snapshot.setup_progress!=SetupProgress::ReadyForInitialUnits &&
        snapshot.setup_progress!=SetupProgress::PrototypeComplete) return false;
    if (snapshot.pending_setup_cell && (!snapshot.pending_cell_setup ||
        *snapshot.pending_setup_cell>=512u*512u || !snapshot.cells.count(*snapshot.pending_setup_cell))) return false;
    try { (void)sim::Random(snapshot.random); } catch (...) { return false; }
    // Flat, ID-sorted indexes. Every check below is a pure test; only the
    // chain pass depends on order, and it keeps ascending Entity ID order.
    std::vector<std::pair<std::uint32_t,const House*>> houses;
    std::vector<std::pair<std::uint32_t,const Entity*>> entities;
    houses.reserve(snapshot.houses.size()); entities.reserve(snapshot.entities.size());
    for (const auto& house:snapshot.houses) {
        if (!house.id || house.name.empty() || house.country.empty()) return false;
        if (house.role && *house.role!=HouseRole::Player && *house.role!=HouseRole::Neutral && *house.role!=HouseRole::Special) return false;
        houses.emplace_back(house.id,&house);
    }
    const auto by_id=[](const auto& a,const auto& b){ return a.first<b.first; };
    std::sort(houses.begin(),houses.end(),by_id);
    for (std::size_t i=1;i<houses.size();++i) if (houses[i-1].first==houses[i].first) return false;
    const auto find=[](const auto& index,std::uint32_t id)->std::size_t {
        const auto it=std::lower_bound(index.begin(),index.end(),id,[](const auto& item,std::uint32_t key){return item.first<key;});
        return it!=index.end() && it->first==id ? std::size_t(it-index.begin()) : index.size();
    };
    for (const auto& entity:snapshot.entities) {
        if (!entity.id || entity.type.empty() ||
            (find(houses,entity.house_id)==houses.size() && !(entity.kind==Kind::Terrain && !entity.house_id))) return false;
        if (entity.kind!=Kind::Unit && entity.kind!=Kind::Infantry &&
            entity.kind!=Kind::Aircraft && entity.kind!=Kind::Structure && entity.kind!=Kind::Terrain) return false;
        if (entity.lifetime!=Lifetime::Created && entity.lifetime!=Lifetime::Placed &&
            entity.lifetime!=Lifetime::Destroyed) return false;
        if (entity.lifetime==Lifetime::Placed && (!entity.position || !entity.cell || !entity.layer)) return false;
        if (entity.layer && *entity.layer!=Layer::Ground && *entity.layer!=Layer::Bridge) return false;
        if (find(houses,entity.id)!=houses.size()) return false; // An ID names one object only.
        entities.emplace_back(entity.id,&entity);
    }
    std::sort(entities.begin(),entities.end(),by_id);
    for (std::size_t i=1;i<entities.size();++i) if (entities[i-1].first==entities[i].first) return false;
    if (snapshot.creation_order.size()!=houses.size()+entities.size()) return false;
    {
        // Each listed ID must name an object of its kind, once.
        std::vector<std::uint8_t> house_listed(houses.size(),0),entity_listed(entities.size(),0);
        for (const auto& object:snapshot.creation_order) {
            if (object.kind==CreatedKind::House) {
                const auto at=find(houses,object.id);
                if (at==houses.size() || house_listed[at]) return false;
                house_listed[at]=1;
            } else if (object.kind==CreatedKind::Entity) {
                const auto at=find(entities,object.id);
                if (at==entities.size() || entity_listed[at]) return false;
                entity_listed[at]=1;
            } else return false;
        }
    }
    const auto entity_at=[&](std::uint32_t id){ return find(entities,id); };
    {
        // complete: the chain from this Entity already passed. walk holds the
        // index of the start Entity whose chain last visited it, plus one.
        std::vector<std::uint8_t> complete(entities.size(),0);
        std::vector<std::size_t> walk(entities.size(),0),chain;
        for (std::size_t start=0;start<entities.size();++start) {
            const auto& entity=*entities[start].second;
            if (entity.next && *entity.next && entity_at(*entity.next)==entities.size()) return false;
            chain.clear();
            auto current=entity.id;
            while (current) {
                const auto at=entity_at(current);
                if (at!=entities.size() && complete[at]) break;
                if (at==entities.size()) {
                    // A missing ID is never the start Entity, which exists.
                    return false;
                }
                if (walk[at]==start+1) return false;
                walk[at]=start+1; chain.push_back(at);
                const auto& item=*entities[at].second;
                if (item.lifetime!=Lifetime::Placed) {
                    if (current==entity.id && (!entity.next || !*entity.next) && entity.lifetime!=Lifetime::Placed) break;
                    return false;
                }
                if (!item.next) break;
                current=*item.next;
            }
            for (const auto at:chain) complete[at]=1;
        }
    }
    // Every check is a pure test, so their order does not change the result.
    // The Cell order check below reads one flag per packed Cell:
    // 1 is allocated, 2 is already listed.
    std::vector<std::uint8_t> allocated;
    if (snapshot.cell_order) allocated.assign(512u*512u,0);
    // Each Cell check reads only its own Cell and the shared Entity index,
    // and writes only its own allocated flag, so any visit order gives the
    // same result.
    const auto check_cell=[&](const std::pair<const std::uint32_t,CellState>& item) {
        const auto& cell=item.second;
        if (!allocated.empty() && item.first<512u*512u && cell.allocated && *cell.allocated) allocated[item.first]=1;
        if (cell.cell_flags_140) {
            const auto same=[&](const std::optional<bool>& field,std::uint32_t bit) {
                return !field || *field==bool(*cell.cell_flags_140 & bit);
            };
            if (!same(cell.cell_flag_80,0x80) || !same(cell.cell_flag_100,0x100) || !same(cell.cell_flag_200,0x200) ||
                !same(cell.cell_flag_20000,0x20000) || !same(cell.cell_flag_40000,0x40000) ||
                (cell.terrain && !same(cell.terrain->bridge,0x100))) return false;
        }
        if (cell.terrain && cell.cell_flag_100 && cell.terrain->bridge &&
            *cell.cell_flag_100!=*cell.terrain->bridge) return false;
        if (item.first>=512*512) return false;
        if (cell.terrain && (cell.terrain->cell.x>=512 || cell.terrain->cell.y>=512 ||
            std::uint32_t(cell.terrain->cell.y)*512+cell.terrain->cell.x!=item.first)) return false;
        if (cell.heads)
            for (const auto head:{cell.heads->ground_head,cell.heads->bridge_head})
                if (head) {
                    const auto at=entity_at(head);
                    if (at==entities.size() || entities[at].second->lifetime!=Lifetime::Placed) return false;
                }
        return true;
    };
    // The Cell map nodes are usually not in the CPU cache, and a map walk
    // waits for each node in turn. Ask the CPU to load nodes ahead, from the
    // node addresses of the previous walk. This hint does not change what
    // is read: the walk below still follows the map itself. A stale address
    // only wastes a load; a prefetch never faults.
    // Addresses stay plain integers; only the prefetch sees a pointer.
    thread_local std::vector<std::uintptr_t> ahead;
    constexpr std::size_t distance=48;
    std::size_t index=0;
    for (auto it=snapshot.cells.begin();it!=snapshot.cells.end();++it,++index) {
        if (index+distance<ahead.size()) {
            const auto node=ahead[index+distance];
            // The map node header sits before the value; the read fields
            // sit in its first 160 bytes.
            for (const std::uintptr_t offset:{std::uintptr_t(0),std::uintptr_t(64),std::uintptr_t(128)})
                __builtin_prefetch(reinterpret_cast<const void*>(node-32+offset));
        }
        const auto here=reinterpret_cast<std::uintptr_t>(&*it);
        if (index<ahead.size()) ahead[index]=here; else ahead.push_back(here);
        if (!check_cell(*it)) return false;
    }
    ahead.resize(index);
    // A listed key at or above 512*512 cannot be an allocated Cell here: the
    // Cell loop above already rejects such a key in the Cell map.
    if (snapshot.cell_order)
        for (const auto key:*snapshot.cell_order) {
            if (key>=512u*512u || allocated[key]!=1) return false;
            allocated[key]=2;
        }
    return true;
}
std::optional<sim::CoarseCategoryInputs> coarse_inputs(const Snapshot& snapshot, std::uint32_t cell) {
    if (snapshot.pending_cell_setup) return {};
    const auto found=snapshot.cells.find(cell);
    if (found==snapshot.cells.end()) return {};
    sim::CoarseCategoryInputs input;
    const auto& state=found->second;
    input.usable=state.usable;
    input.overlay=state.overlay;
    input.land_wheel=state.land_wheel;
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
