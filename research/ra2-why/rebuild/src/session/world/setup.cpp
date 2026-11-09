#include "setup.h"
#include "sim/floor.h"
#include "sim/world_init.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ra2::session::world {
namespace {
rules::IniFile read_ini(fs::MixFileSystem& files,const std::string& name,bool external=false) {
    if (external && std::filesystem::is_regular_file(name)) return rules::IniFile::from_file(name);
    const auto data=files.read_file(name);
    if (!data) throw std::runtime_error("Missing original game file: "+name);
    rules::IniFile result;
    result.parse(std::string_view(reinterpret_cast<const char*>(data->data()),data->size()));
    return result;
}
MapRectangle rectangle(const rules::IniFile& ini,const char* key) {
    const auto values=ini.read_list("Map",key);
    if (values.size()!=4) throw std::runtime_error(std::string("Invalid map ")+key);
    std::int32_t result[4];
    for (std::size_t i=0;i<4;++i) {
        const auto number=rules::IniFile::integer_value(values[i]);
        if (!number) throw std::runtime_error(std::string("Invalid map ")+key);
        result[i]=*number;
    }
    return {result[0],result[1],result[2],result[3]};
}
}
struct BaseBattleAssets::Data {
    explicit Data(const std::filesystem::path& game):files(game) {}
    fs::MixFileSystem files;
    rules::IniFile rules,art_ini,map_ini,mode_catalog,battle_rules;
    std::string battle_rules_name;
    ra2map::MapFile map;
    std::unique_ptr<render::TheaterArt> theater;
    PlacementGeometry geometry;
    std::vector<BaseMapCellInput> cells;
    std::vector<BaseOverlayInput> overlays;
};
BaseBattleAssets::BaseBattleAssets(std::unique_ptr<Data> data):data_(std::move(data)) {}
BaseBattleAssets::~BaseBattleAssets()=default;
BaseBattleAssets::BaseBattleAssets(BaseBattleAssets&&) noexcept=default;
BaseBattleAssets& BaseBattleAssets::operator=(BaseBattleAssets&&) noexcept=default;
BaseBattleAssets BaseBattleAssets::load(const std::filesystem::path& game,const std::string& map_name,const std::string& map_archive) {
    if (map_name.empty()) throw std::invalid_argument("Map name is empty");
    auto data=std::make_unique<Data>(game);
    std::optional<std::vector<std::uint8_t>> selected_map;
    if (!map_archive.empty()) {
        const std::filesystem::path archive_path(map_archive);
        auto extension=archive_path.extension().string();
        for(auto& c:extension)if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if (archive_path.filename()!=archive_path || extension!=".MMX")
            throw std::invalid_argument("Base multiplayer map archive must be an installed MMX file");
        const auto archive=data->files.mount(map_archive);
        const auto* entry=archive->find(map_name);
        if (!entry)throw std::runtime_error("Selected map archive does not contain: "+map_name);
        selected_map=archive->read(*entry);
    }
    data->files.mount_language(fs::GameVariant::RedAlert2);
    data->files.bootstrap(fs::GameVariant::RedAlert2);
    data->files.mount_overlays();
    data->files.mount_resources();
    data->rules=read_ini(data->files,"RULES.INI");
    data->art_ini=read_ini(data->files,"ART.INI");
    data->mode_catalog=read_ini(data->files,"MPModes.ini");
    const auto* battle=data->mode_catalog.find("Battle","1");
    if (!battle) throw std::runtime_error("Missing original Battle mode");
    const auto mode_fields=rules::IniFile::split_list(battle->value);
    if (mode_fields.size()<4 || mode_fields[2].empty())
        throw std::runtime_error("Invalid original Battle mode");
    data->battle_rules_name=mode_fields[2];
    data->battle_rules=read_ini(data->files,data->battle_rules_name);
    if(selected_map)data->map_ini.parse(std::string_view(
        reinterpret_cast<const char*>(selected_map->data()),selected_map->size()));
    else data->map_ini=read_ini(data->files,map_name,true);
    const auto geometry=resolve_placement_geometry(rectangle(data->map_ini,"Size"),rectangle(data->map_ini,"LocalSize"));
    if (!geometry) throw std::runtime_error("Unsupported map geometry");
    data->geometry=*geometry;
    data->map.parse(data->map_ini);
    const auto theater=data->map_ini.read_string("Map","Theater");
    if (theater.empty()) throw std::runtime_error("Missing map theater");
    data->theater=std::make_unique<render::TheaterArt>(render::TheaterArt::load(data->files,theater));
    const auto& packed=data->map.packed_terrain();
    constexpr std::size_t grid=512u*512u;
    if ((!packed.overlay.empty() && packed.overlay.size()!=grid) ||
        (!packed.overlay_data.empty() && packed.overlay_data.size()!=grid))
        throw std::runtime_error("Invalid packed overlay grid");
    for (std::size_t i=0;i<packed.overlay.size();++i) if (packed.overlay[i]!=255) {
        data->overlays.push_back({{static_cast<std::uint16_t>(i%512),static_cast<std::uint16_t>(i/512)},
                                  packed.overlay[i]});
    }
    std::map<std::pair<std::uint16_t,std::uint16_t>,const pack::Tile*> records;
    for (const auto& tile:packed.tiles) records[{tile.x,tile.y}]=&tile;
    const auto order=sim::original_map_cell_order(geometry->width,geometry->height);
    data->cells.reserve(order.size());
    for (const auto cell:order) {
        const auto it=records.find({cell.x,cell.y});
        const pack::Tile fallback{cell.x,cell.y,0xffff,0,0,0};
        const auto& tile=it==records.end()?fallback:*it->second;
        const auto index=std::size_t(cell.y)*512+cell.x;
        const bool present=it!=records.end();
        data->cells.push_back({cell,present?std::optional<std::uint32_t>{tile.index}:std::nullopt,
            present?std::optional<std::uint32_t>{data->theater->translate_map_index(tile.index)}:std::nullopt,
            tile.subtile,tile.height,tile.ice,
            packed.overlay.empty()?std::optional<std::uint8_t>{}:packed.overlay[index],
            packed.overlay_data.empty()?std::optional<std::uint8_t>{}:packed.overlay_data[index]});
    }
    return BaseBattleAssets(std::move(data));
}
const rules::IniFile& BaseBattleAssets::rules() const { return data_->rules; }
const rules::IniFile& BaseBattleAssets::art_ini() const { return data_->art_ini; }
const rules::IniFile& BaseBattleAssets::mode_catalog() const { return data_->mode_catalog; }
const rules::IniFile& BaseBattleAssets::battle_rules() const { return data_->battle_rules; }
const std::string& BaseBattleAssets::battle_rules_name() const { return data_->battle_rules_name; }
const rules::IniFile& BaseBattleAssets::map_ini() const { return data_->map_ini; }
const ra2map::MapFile& BaseBattleAssets::map() const { return data_->map; }
render::TheaterArt& BaseBattleAssets::theater() { return *data_->theater; }
std::optional<std::vector<std::uint8_t>> BaseBattleAssets::read_asset(const std::string& name) const {
    return data_->files.read_file(name);
}
const PlacementGeometry& BaseBattleAssets::geometry() const { return data_->geometry; }
const std::vector<BaseMapCellInput>& BaseBattleAssets::cells_in_original_order() const { return data_->cells; }
const std::vector<BaseOverlayInput>& BaseBattleAssets::overlays_in_scan_order() const { return data_->overlays; }


std::optional<PackedMapState> read_base_packed_map(const BaseBattleAssets& assets,
                                                   std::optional<std::uint32_t> default_live_tile) {
    PackedMapState result;
    result.geometry=assets.geometry();
    result.cell_order.reserve(assets.cells_in_original_order().size());
    for (const auto& input:assets.cells_in_original_order()) {
        if (!input.translated_tile && !default_live_tile) return {};
        const auto key=std::uint32_t(input.cell.y)*512+input.cell.x;
        CellState state;
        state.allocated=true;
        state.heads=weapons::CellContentHeads{};
        state.cell_field_54=-1; state.cell_field_58=-1; state.tube_field_116=-1;
        state.neighbor_count=0;
        // The constructor proves only byte +124 is zero. The upper three
        // bytes of the occupancy word are not resolved at this boundary.
        // The reader's proven state precedes any Overlay and object loaders.
        state.cell_flag_80=false; state.cell_flag_100=false;
        sim::TerrainCell terrain;
        terrain.cell=input.cell;
        terrain.tile_index=input.packed_tile ? *input.packed_tile : *default_live_tile;
        terrain.resolved_tile_index=input.translated_tile ? input.translated_tile : default_live_tile;
        terrain.subtile=input.subtile;
        terrain.height_level=input.height<128 ? int(input.height) : int(input.height)-256;
        terrain.bridge=false;
        state.terrain=std::move(terrain);
        if (!result.cells.emplace(key,std::move(state)).second) return {};
        result.cell_order.push_back(key);
    }
    return result;
}


std::optional<Snapshot> packed_map_snapshot(const PackedMapState& map,const sim::RandomState& random) {
    std::vector<ra2map::Cell> expected;
    try { expected=sim::original_map_cell_order(map.geometry.width,map.geometry.height); }
    catch (const std::invalid_argument&) { return {}; }
    if (expected.size()!=map.cell_order.size() || expected.size()!=map.cells.size()) return {};
    for (std::size_t i=0;i<expected.size();++i)
        if (map.cell_order[i]!=std::uint32_t(expected[i].y)*512+expected[i].x) return {};
    Snapshot result;
    result.setup_progress=SetupProgress::PackedMapRead;
    result.geometry=map.geometry;
    result.cell_order=map.cell_order;
    result.cells=map.cells;
    result.random=random;
    if (!validate(result)) return {};
    return result;
}



bool apply_base_overlay_data(Snapshot& world,const BaseBattleAssets& assets) {
    if (world.setup_progress!=SetupProgress::OverlayObjectsRead || world.pending_cell_setup ||
        !world.geometry || !world.cell_order || !validate(world)) return false;
    const auto& geom=assets.geometry();
    if (world.geometry->width!=geom.width || world.geometry->height!=geom.height ||
        world.cell_order->size()!=assets.cells_in_original_order().size()) return false;
    const auto& data=assets.map().packed_terrain().overlay_data;
    if (!data.empty() && data.size()!=512u*512u) return false;
    auto result=world;
    for (std::size_t i=0;i<assets.cells_in_original_order().size();++i) {
        const auto& input=assets.cells_in_original_order()[i];
        const auto key=std::uint32_t(input.cell.y)*512+input.cell.x;
        if ((*world.cell_order)[i]!=key) return false;
        auto found=result.cells.find(key);
        if (found==result.cells.end() || found->second.allocated!=true) return false;
        // The original scan visits all 512*512 bytes in row order, but only
        // allocated diamond cells receive writes. Each is written once.
        if (!data.empty()) found->second.overlay_data_11e=data[key];
    }
    result.setup_progress=SetupProgress::OverlayDataRead;
    if (!validate(result)) return false;
    world=std::move(result);
    return true;
}

sim::Coord battle_start_position(StartPoint point) noexcept {
    return {int(point.x)*256+128,int(point.y)*256+128,0};
}

std::optional<DirectBaseUnitResult> place_direct_base_unit(Snapshot& world,
    const InitialBaseUnitInput& input,const sim::CellSetupStatic& cell_setup,
    const sim::TheaterInputs& theater,std::uint8_t cliff,const std::map<std::int32_t,float>& wheels) {
    if (world.setup_progress!=SetupProgress::ReadyForInitialUnits || input.start.x<0 || input.start.y<0 ||
        input.start.x>=512 || input.start.y>=512) return {};
    const auto key=std::uint32_t(input.start.y)*512+input.start.x;
    const auto cell=world.cells.find(key);
    if (cell==world.cells.end() || !cell->second.terrain || !cell->second.terrain->floor_world_height ||
        input.start_position.x!=int(input.start.x)*256+128 || input.start_position.y!=int(input.start.y)*256+128 ||
        (input.start_position.z!=0 && input.start_position.z!=*cell->second.terrain->floor_world_height)) return {};
    try {
        Stage working(world);
        const auto unit=working.create_unit(input.house_id,input.type,input.maximum_hp,
                                             input.type_index,input.excluded_from_house_tracking);
        if (!unit) return {};
        const auto placed=working.place_base_unit_at(*unit,input.start,input.start_position.z,0,input.profile);
        if (placed==PlacementResult::Unresolved) return {};
        if (placed==PlacementResult::Placed &&
            working.complete_pending_cell_setup(cell_setup,theater,cliff,wheels).status!=sim::CellSetupStatus::Complete)
            return {};
        auto result=working.snapshot();
        for (auto& entity:result.entities) if (entity.id==*unit) entity.maximum_hp=input.maximum_hp;
        world=std::move(result);
        return DirectBaseUnitResult{*unit,placed};
    } catch (const std::invalid_argument&) { return {}; }
}


std::optional<StartPoint> search_blocked_base_unit(Snapshot& world,std::uint32_t unit_id,
    StartPoint origin,PlacementBounds bounds,const BaseUnitProfile& profile,
    const sim::CellSetupStatic& cell_setup,const sim::TheaterInputs& theater,std::uint8_t cliff,
    const std::map<std::int32_t,float>& wheels) {
    if (world.setup_progress!=SetupProgress::ReadyForInitialUnits || !world.geometry || !validate(world)) return {};
    auto candidate=world;
    try {
        sim::Random random(candidate.random);
        const auto valid=[&](StartPoint point,sim::Random&)->std::optional<bool> {
            return placement_cell_in_map(point,PlacementEdition::Base,*candidate.geometry,{});
        };
        const auto place=[&](StartPoint point,sim::Random& state)->std::optional<bool> {
            auto trial=candidate;
            trial.random=state.state();
            Stage working(std::move(trial));
            const auto placed=working.place_base_unit(unit_id,point,0,profile);
            if (placed==PlacementResult::Unresolved) return {};
            if (placed==PlacementResult::Placed &&
                working.complete_pending_cell_setup(cell_setup,theater,cliff,wheels).status!=sim::CellSetupStatus::Complete)
                return {};
            candidate=working.snapshot();
            state=sim::Random(candidate.random);
            return placed==PlacementResult::Placed;
        };
        const auto found=find_initial_placement_checked(origin,bounds,1,PlacementEdition::Base,random,valid,place);
        if (!found || !found->point) return {};
        candidate.random=random.state();
        world=std::move(candidate);
        return found->point;
    } catch (const std::invalid_argument&) { return {}; }
}

std::optional<Snapshot> construct_house_fragments(const sim::RandomState& before,
    const std::vector<PlayerHouseSetup>& players,std::int32_t registry_size,
    std::optional<std::int32_t> light_grey,std::optional<std::int32_t> neutral_index,
    std::optional<std::int32_t> special_index) {
    if (players.empty() || players.size()>8) return {};
    try {
        Stage stage(before);
        for (const auto& player:players)
            if (!stage.create_player_house(player.name,player.country,player.credits,
                                          player.selected_color,registry_size,player.original_index)) return {};
        if (!stage.create_auxiliary_house(HouseRole::Neutral,registry_size,light_grey,neutral_index) ||
            !stage.create_auxiliary_house(HouseRole::Special,registry_size,light_grey,special_index)) return {};
        return stage.snapshot();
    } catch (const std::invalid_argument&) { return {}; }
}

std::optional<PrototypeBattle> prototype_battle(BaseBattleAssets& assets,
    const PrototypeBattleSetup& options,const sim::RandomState& before) {
    if (options.players.size()<2 || options.players.size()>8) return {};
    const auto& rules=assets.rules();
    const auto& mode=assets.battle_rules();
    const auto& map=assets.map_ini();
    const auto value=[&](const std::string& section,const char* key) -> const rules::IniFile::Entry* {
        if (const auto* entry=map.find(section,key)) return entry;
        if (const auto* entry=mode.find(section,key)) return entry;
        return rules.find(section,key);
    };
    const auto integer=[&](const std::string& section,const char* key) -> std::optional<std::int32_t> {
        const auto* entry=value(section,key);
        return entry ? rules::IniFile::integer_value(entry->value) : std::nullopt;
    };
    const auto list=[&](const std::string& section,const char* key) {
        const auto* entry=value(section,key);
        return entry ? rules::IniFile::split_list(entry->value) : std::vector<std::string>{};
    };
    const auto* countries=rules.find_section("Countries");
    const auto* vehicles=rules.find_section("VehicleTypes");
    if (!countries || !vehicles || vehicles->entries.empty() || vehicles->entries.size()>512) return {};
    const auto vehicle_index=[&](const std::string& type) -> std::optional<std::int32_t> {
        const auto it=std::find_if(vehicles->entries.begin(),vehicles->entries.end(),
            [&](const auto& entry){ return entry.value==type; });
        if (it==vehicles->entries.end()) return {};
        return static_cast<std::int32_t>(it-vehicles->entries.begin());
    };
    const auto country_index=[&](const std::string& country) -> std::optional<std::int32_t> {
        const auto it=std::find_if(countries->entries.begin(),countries->entries.end(),
            [&](const auto& entry){ return entry.value==country; });
        if (it==countries->entries.end()) return {};
        return static_cast<std::int32_t>(it-countries->entries.begin());
    };
    auto starts=assets.map().waypoints();
    std::sort(starts.begin(),starts.end(),[](const auto& a,const auto& b){return a.index<b.index;});
    std::set<int> used_waypoints;
    std::set<std::string> used_names;
    std::vector<ra2map::Cell> chosen_starts;
    std::vector<std::string> mcvs;
    std::vector<PlayerHouseSetup> players;
    for (const auto& player:options.players) {
        if (player.name.empty() || player.name=="Neutral" || player.name=="Special" ||
            !used_names.insert(player.name).second || !country_index(player.country) || player.credits<0 ||
            player.color<0 || player.color>7 || player.vehicle_count<0 || player.vehicle_count>24) return {};
        const auto waypoint=std::find_if(starts.begin(),starts.end(),[&](const auto& start){
            return player.waypoint_index<0 ? !used_waypoints.count(start.index) : start.index==player.waypoint_index;
        });
        if (waypoint==starts.end() || !used_waypoints.insert(waypoint->index).second) return {};
        chosen_starts.push_back(waypoint->cell);
        std::string mcv;
        for (const auto& name:list("General","BaseUnit")) {
            const auto owners=list(name,"Owner");
            if (std::find(owners.begin(),owners.end(),player.country)!=owners.end()) { mcv=name; break; }
        }
        if (mcv.empty() || !vehicle_index(mcv)) return {};
        mcvs.push_back(mcv);
        players.push_back({player.name,player.country,player.credits,player.color,
                           static_cast<std::int32_t>(players.size())});
    }
    const auto houses=construct_house_fragments(before,players,static_cast<std::int32_t>(vehicles->entries.size()),
                                                 std::nullopt,static_cast<std::int32_t>(players.size()),
                                                 static_cast<std::int32_t>(players.size()+1));
    if (!houses) return {};
    Stage stage(*houses);
    PrototypeBattle result;
    auto& world=result.snapshot;
    world=stage.snapshot();
    const auto packed=read_base_packed_map(assets,0xffff);
    if (!packed) return {};
    world.geometry=packed->geometry;
    world.cell_order=packed->cell_order;
    world.cells=packed->cells;
    world.setup_progress=SetupProgress::PrototypeComplete;
    // TODO-ORIGINAL: replace these session fields with the real load results.
    world.tube_vector_count=0;
    world.session_field_838=0;
    // TODO-ORIGINAL: run overlay, bridge, object, visibility and Cell setup.
    // Until then, use raw map overlays, TMP floor planes, and empty flags.
    std::size_t fallback_tiles=0;
    for (const auto& input:assets.cells_in_original_order()) {
        const auto key=std::uint32_t(input.cell.y)*512+input.cell.x;
        auto& state=world.cells.at(key);
        auto& terrain=*state.terrain;
        terrain.overlay_type_index=input.packed_overlay && *input.packed_overlay!=255 ? int(*input.packed_overlay) : -1;
        terrain.bridge=false;
        terrain.ramp_index=0;
        if (terrain.resolved_tile_index && *terrain.resolved_tile_index!=0xffff) {
            try {
                const auto count=assets.theater().resolved_subtile_count(*terrain.resolved_tile_index);
                if (count) {
                    const auto sub=static_cast<std::uint8_t>(terrain.subtile%count);
                    if (assets.theater().resolved_subtile_present(*terrain.resolved_tile_index,sub)) {
                        const auto& metadata=assets.theater().resolved_metadata(*terrain.resolved_tile_index,sub);
                        terrain.tmp_terrain=metadata.terrain;
                        if (metadata.ramp<=20) terrain.ramp_index=metadata.ramp;
                    }
                }
            } catch (const render::RenderError&) {}
        }
        if (terrain.tmp_terrain && *terrain.tmp_terrain<16) terrain.land_type=sim::original_tmp_land_type(*terrain.tmp_terrain);
        else { terrain.land_type=0; ++fallback_tiles; }
        terrain.floor_world_height=sim::original_floor_height(static_cast<std::int8_t>(*terrain.height_level),
            static_cast<std::uint8_t>(*terrain.ramp_index),int(input.cell.x)*256+128,int(input.cell.y)*256+128);
        state.overlay_data_11e=input.packed_overlay_data;
        state.cell_flags_140=0; state.cell_flag_80=false; state.cell_flag_100=false;
        state.cell_flag_200=false; state.cell_flag_20000=false; state.cell_flag_40000=false;
        state.ground_occupancy_124=0; state.bridge_occupancy_128=0;
        state.cell_field_12c=0; state.usable=true;
        const bool passable=*terrain.land_type!=2 && *terrain.land_type!=3;
        state.land_wheel=passable ? 1.0f : 0.0f;
        state.unit_land_speed=state.land_wheel;
        state.coarse_category=passable ? 0 : 4;
    }
    for (std::size_t i=0;i<world.houses.size();++i) {
        auto& house=world.houses[i];
        // TODO-ORIGINAL: replace these multiplayer House defaults with its load phase.
        house.constructor_complete=true; house.human_controlled=i<options.players.size() && options.players[i].human;
        house.house_flag_135=false; house.observer_flag_13d=false; house.tech_level=10;
        house.power_output=0; house.power_drain=0; house.reload_multiplier=1.0; house.firepower_multiplier=1.0;
        house.stolen_tech_204=false; house.stolen_tech_205=false;
        house.stolen_mask_208=0; house.stolen_mask_20c=0; house.stolen_mask_210=0; house.stolen_mask_214=0;
        house.buildings_active_5460.emplace(); house.buildings_owned_5420.emplace(); house.buildings_ever_54a0.emplace();
        house.units_owned_5430.emplace(); house.units_ever_54b0.emplace(); house.infantry_owned_5440.emplace();
        house.infantry_ever_54c0.emplace(); house.aircraft_owned_5450.emplace(); house.aircraft_ever_54d0.emplace();
        house.attacker_damage_records.emplace(); house.attacker_damage_count=0; house.selected_enemy_index=-1;
        house.alliance_mask_5674=i<32 ? std::uint32_t{1}<<i : 0;
        if (i<options.players.size()) {
            house.country_index=country_index(options.players[i].country);
            house.side_index=mcvs[i]=="AMCV" ? 0 : 1;
        }
    }
    world.veteran_reload_multiplier=1.0; world.veteran_firepower_multiplier=1.0;
    sim::Random random(world.random);
    std::uint32_t next_id=0;
    for (const auto& object:world.creation_order) next_id=std::max(next_id,object.id);
    const auto next=[&]() { return ++next_id; };
    const auto house_id=[&](const std::string& name) -> std::uint32_t {
        for (const auto& house:world.houses) if (house.name==name) return house.id;
        // TODO-ORIGINAL: map House construction and alliances need their real phases.
        House house; house.id=next(); house.name=name;
        house.country=map.read_string(name,"Country",name); house.role=HouseRole::Neutral;
        house.constructor_complete=true; house.human_controlled=false; house.cash=0;
        world.creation_order.push_back({CreatedKind::House,house.id});
        world.houses.push_back(house);
        return house.id;
    };
    const auto place=[&](Entity& entity,ra2map::Cell cell,int facing) -> bool {
        const auto key=std::uint32_t(cell.y)*512+cell.x;
        auto it=world.cells.find(key);
        if (it==world.cells.end() || !it->second.terrain || !it->second.terrain->floor_world_height) return false;
        auto& state=it->second;
        entity.cell=cell; entity.layer=Layer::Ground; entity.lifetime=Lifetime::Placed;
        entity.position=sim::Coord{int(cell.x)*256+128,int(cell.y)*256+128,*state.terrain->floor_world_height};
        entity.next=state.heads->ground_head; state.heads->ground_head=entity.id;
        // TODO-ORIGINAL: object footprints, Cell marking, and occupancy updates.
        *state.ground_occupancy_124|=0x20;
        entity.hull_facing=facing&255; entity.hull_facing_raw=static_cast<std::uint16_t>((facing&255)<<8);
        entity.turret_facing=facing&255; entity.turret_facing_raw=entity.hull_facing_raw;
        entity.on_map=true; entity.active=true; entity.in_limbo=false; entity.collision_eligible=true;
        // TODO-ORIGINAL: run each object's full unlimbo and mark callbacks.
        entity.object_field_74=1; entity.object_field_75=1; entity.object_field_76=0;
        entity.collision_virtual_position=entity.position; entity.has_trigger=false; entity.target_id=0;
        entity.constructor_complete=true; entity.locomotor_moving=false; entity.mission=0;
        entity.burst_index=0; entity.weapon_timer_start_frame=-1; entity.weapon_timer_duration=0;
        entity.armor_multiplier=1.0; entity.house_armor_multiplier=1.0; entity.firepower_multiplier=1.0;
        entity.veterancy=0.0; entity.techno_field_1b8=0; entity.techno_field_329=0;
        return true;
    };
    std::size_t skipped=0;
    const auto actor=[&](Kind kind,const std::string& name,const std::string& type,ra2map::Cell cell,
                         int facing,int map_health) {
        if (type.empty() || (kind!=Kind::Terrain && name.empty())) { ++skipped; return; }
        Entity entity; entity.id=next(); entity.kind=kind; entity.type=type;
        entity.house_id=kind==Kind::Terrain ? 0 : house_id(name);
        // TODO-ORIGINAL: construction draws for map actors have not been recovered as a whole.
        if (kind!=Kind::Terrain) entity.constructor_random=static_cast<std::uint16_t>(random.next());
        if (const auto strength=integer(type,"Strength"); strength && *strength>0) {
            entity.maximum_hp=*strength;
            const auto scaled=std::int64_t(std::clamp(map_health,0,256))* *strength/256;
            entity.resolved_hp=static_cast<std::int32_t>(std::max<std::int64_t>(1,scaled));
        }
        entity.category.emplace();
        entity.category->runtime_type=kind==Kind::Terrain ? 36 : kind==Kind::Structure ? 6 : 1;
        if (!place(entity,cell,facing)) { ++skipped; return; }
        world.creation_order.push_back({CreatedKind::Entity,entity.id});
        world.entities.push_back(std::move(entity));
    };
    for (const auto& object:assets.map().units()) actor(Kind::Unit,object.house,object.type,object.cell,object.facing,object.health);
    for (const auto& object:assets.map().aircraft()) actor(Kind::Aircraft,object.house,object.type,object.cell,object.facing,object.health);
    for (const auto& object:assets.map().infantry()) actor(Kind::Infantry,object.house,object.type,object.cell,object.facing,object.health);
    for (const auto& object:assets.map().structures()) actor(Kind::Structure,object.house,object.type,object.cell,object.facing,object.health);
    for (const auto& object:assets.map().terrain()) actor(Kind::Terrain,"",object.type,object.cell,0,256);
    // Base Battle 005B7CE0 uses Unit Count budget slots in addition to
    // the MCV. Legacy generator 00660320 instead subtracts a base slot.
    struct StartType { std::string name; int cost,index; Kind kind; };
    std::vector<StartType> start_types;
    int total_cost=0;
    const auto bases=list("General","BaseUnit");
    for (const auto kind:{Kind::Unit,Kind::Infantry}) {
        const auto* registry=rules.find_section(kind==Kind::Unit ? "VehicleTypes" : "InfantryTypes");
        if (!registry) return {};
        for (std::size_t index=0;index<registry->entries.size();++index) {
            const auto& name=registry->entries[index].value;
            const auto* flag=value(name,"AllowedToStartInMultiplayer");
            if (flag && flag->value!="yes" && flag->value!="true" && flag->value!="1") continue;
            if (kind==Kind::Unit && std::find(bases.begin(),bases.end(),name)!=bases.end()) continue;
            const auto owners=list(name,"Owner");
            if (integer(name,"TechLevel").value_or(0)>10 ||
                std::none_of(options.players.begin(),options.players.end(),[&](const auto& player) {
                    return std::find(owners.begin(),owners.end(),player.country)!=owners.end();
                })) continue;
            const auto cost=integer(name,"Cost");
            if (!cost) continue;
            if (*cost<=0) return {};
            start_types.push_back({name,*cost,static_cast<int>(index),kind}); total_cost+=*cost;
        }
    }
    if (start_types.empty()) return {};
    const int average_cost=(total_cost+static_cast<int>(start_types.size())/2)/static_cast<int>(start_types.size());
    for (std::size_t player=0;player<options.players.size();++player) {
        std::vector<StartType> tanks,infantry;
        for (const auto& type:start_types) {
            const auto owners=list(type.name,"Owner");
            if (std::find(owners.begin(),owners.end(),options.players[player].country)==owners.end() ||
                integer(type.name,"TechLevel").value_or(0)>10) continue;
            (type.kind==Kind::Unit ? tanks : infantry).push_back(type);
        }
        const int budget=average_cost*options.players[player].vehicle_count;
        int spent=0;
        bool base=true;
        while (base || spent<budget) {
            StartType type{mcvs[player],0,*vehicle_index(mcvs[player]),Kind::Unit};
            if (!base) {
                const auto& pool=spent<budget*2/3 && !tanks.empty() ? tanks : infantry;
                if (pool.empty()) return {};
                type=pool[random.ranged(0,static_cast<int>(pool.size())-1)];
            }
            // Techno constructor 006C12AA consumes one word for both classes.
            Entity entity; entity.id=next(); entity.house_id=world.houses[player].id;
            entity.type=type.name; entity.kind=type.kind; entity.original_type_index=type.index;
            entity.resolved_hp=integer(type.name,"Strength"); entity.maximum_hp=entity.resolved_hp;
            entity.constructor_random=static_cast<std::uint16_t>(random.next());
            entity.category.emplace(); entity.category->runtime_type=type.kind==Kind::Infantry ? 15 : 1;
            entity.occupancy_tracking=true; entity.excluded_from_house_tracking=false;
            entity.techno_field_370=0; entity.unit_field_7d=1; entity.foot_field_5a1=0;
            const auto origin=chosen_starts[player];
            const auto try_place=[&](StartPoint point,sim::Random& placement_random)->std::optional<bool> {
                if (point.x<0 || point.y<0 || point.x>=512 || point.y>=512) return false;
                const auto found=world.cells.find(std::uint32_t(point.y)*512+point.x);
                if (found==world.cells.end()) return false;
                const auto& cell=found->second;
                if (!cell.unit_land_speed || *cell.unit_land_speed<=0 || !cell.ground_occupancy_124 ||
                    cell.terrain->overlay_type_index!=-1) return false;
                const auto bits=*cell.ground_occupancy_124;
                if (type.kind==Kind::Unit && bits) return false;
                unsigned subcell=2;
                if (type.kind==Kind::Infantry) {
                    if (bits&0x20) return false;
                    // Base 004764B0 selects one of four preference rows.
                    // Original bytes at 007D2B80 rotate [1,2,3,4].
                    const unsigned preference=placement_random.ranged(0,3);
                    bool free=false;
                    for (unsigned n=0;n<4;++n) {
                        subcell=(preference+n)%4+1;
                        if (subcell!=1 && !(bits&(1u<<subcell))) { free=true; break; }
                    }
                    if (!free) return false;
                }
                if (type.kind==Kind::Infantry && cell.heads->ground_head) {
                    for (const auto& other:world.entities)
                        if (other.cell && other.cell->x==point.x && other.cell->y==point.y &&
                            (other.kind!=Kind::Infantry || other.house_id!=entity.house_id)) return false;
                }
                const bool placed=place(entity,{static_cast<std::uint16_t>(point.x),static_cast<std::uint16_t>(point.y)},base ? 128 : 0);
                if (placed && type.kind==Kind::Infantry) {
                    // Base 00508410 marks an infantry position. Slots 0/1
                    // are special; live infantry uses slots 2..4.
                    // Initializer 00483390 fills the table at 00850E50.
                    static constexpr int offset_x[5]={128,64,192,64,192};
                    static constexpr int offset_y[5]={128,64,64,192,192};
                    entity.position->x=int(point.x)*256+offset_x[subcell];
                    entity.position->y=int(point.y)*256+offset_y[subcell];
                    entity.position->z=sim::original_floor_height(
                        static_cast<std::int8_t>(*cell.terrain->height_level),
                        static_cast<std::uint8_t>(*cell.terrain->ramp_index),
                        entity.position->x,entity.position->y);
                    entity.collision_virtual_position=entity.position;
                    *found->second.ground_occupancy_124=bits|(1u<<subcell);
                }
                return placed;
            };
            const auto valid=[&](StartPoint point,sim::Random&)->std::optional<bool> {
                return world.cells.count(std::uint32_t(point.y)*512+point.x)!=0;
            };
            // Base 00660B60: direct attempt, then random compass direction,
            // eight candidates, then eight jittered candidates per radius.
            const auto found=find_initial_placement_checked(
                {static_cast<std::int16_t>(origin.x),static_cast<std::int16_t>(origin.y)},
                {1,1,static_cast<std::int16_t>(world.geometry->width+world.geometry->height-2),
                     static_cast<std::int16_t>(world.geometry->width+world.geometry->height-2)},
                base ? 1 : 4,PlacementEdition::Base,random,valid,try_place);
            if (!found || !found->point) return {};
            auto& house=world.houses[player];
            if (type.kind==Kind::Unit) {
                ++*house.unit_count; ++(*house.unit_type_counts)[type.index];
                ++(*house.unit_fixed_counts)[type.index]; ++(*house.placed_unit_counts)[type.index];
                ++(*house.units_owned_5430)[type.index]; ++(*house.units_ever_54b0)[type.index];
            } else {
                ++(*house.infantry_owned_5440)[type.index]; ++(*house.infantry_ever_54c0)[type.index];
            }
            world.creation_order.push_back({CreatedKind::Entity,entity.id});
            world.entities.push_back(std::move(entity));
            if (!base) world.entities.back().mission=static_cast<int>(starting_unit_mission(options.players[player].human));
            spent+=type.cost; base=false;
        }
    }
    finish_starting_unit_generation(random);
    world.random=random.state();
    if (!validate(world)) return {};
    result.gaps={"Map object footprints, overlay effects, bridge state, and House updates are incomplete.",
        "The shared random state is preserved, but original setup draw order is incomplete."};
    if (fallback_tiles) result.gaps.push_back("Some terrain tiles use prototype land defaults: "+std::to_string(fallback_tiles));
    if (skipped) result.gaps.push_back("Some map objects could not be placed: "+std::to_string(skipped));
    return result;
}
}
