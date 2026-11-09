#include "setup.h"
#include "sim/world_init.h"
#include <map>
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
    rules::IniFile rules,art_ini,map_ini;
    ra2map::MapFile map;
    std::unique_ptr<render::TheaterArt> theater;
    PlacementGeometry geometry;
    std::vector<BaseMapCellInput> cells;
};
BaseBattleAssets::BaseBattleAssets(std::unique_ptr<Data> data):data_(std::move(data)) {}
BaseBattleAssets::~BaseBattleAssets()=default;
BaseBattleAssets::BaseBattleAssets(BaseBattleAssets&&) noexcept=default;
BaseBattleAssets& BaseBattleAssets::operator=(BaseBattleAssets&&) noexcept=default;
BaseBattleAssets BaseBattleAssets::load(const std::filesystem::path& game,const std::string& map_name) {
    if (map_name.empty()) throw std::invalid_argument("Map name is empty");
    auto data=std::make_unique<Data>(game);
    data->files.mount_language(fs::GameVariant::RedAlert2);
    data->files.bootstrap(fs::GameVariant::RedAlert2);
    data->files.mount_overlays();
    data->files.mount_resources();
    data->rules=read_ini(data->files,"RULES.INI");
    data->art_ini=read_ini(data->files,"ART.INI");
    data->map_ini=read_ini(data->files,map_name,true);
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
const rules::IniFile& BaseBattleAssets::map_ini() const { return data_->map_ini; }
const ra2map::MapFile& BaseBattleAssets::map() const { return data_->map; }
render::TheaterArt& BaseBattleAssets::theater() { return *data_->theater; }
const PlacementGeometry& BaseBattleAssets::geometry() const { return data_->geometry; }
const std::vector<BaseMapCellInput>& BaseBattleAssets::cells_in_original_order() const { return data_->cells; }


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
}
