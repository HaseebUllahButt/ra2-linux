#pragma once
#include "world.h"
#include "render/minimap.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ra2::session::world {
// A packed map record after allocation, before any original map setup pass.
// Packed overlay bytes are inputs to the overlay loader, not live Cell flags.
struct BaseMapCellInput {
    ra2map::Cell cell;
    std::optional<std::uint32_t> packed_tile, translated_tile;
    std::uint8_t subtile, height, ice;
    std::optional<std::uint8_t> packed_overlay, packed_overlay_data;
};

struct BaseOverlayInput {
    ra2map::Cell cell;
    std::uint8_t type_index;
};

// Owns the filesystem and theater used by the static asset readers. None of
// these authored inputs is a finished runtime world or an RNG phase boundary.
class BaseBattleAssets {
public:
    static BaseBattleAssets load(const std::filesystem::path& game_directory,
                                 const std::string& map_name_or_path,
                                 const std::string& map_archive={});
    ~BaseBattleAssets();
    BaseBattleAssets(BaseBattleAssets&&) noexcept;
    BaseBattleAssets& operator=(BaseBattleAssets&&) noexcept;
    BaseBattleAssets(const BaseBattleAssets&)=delete;
    BaseBattleAssets& operator=(const BaseBattleAssets&)=delete;
    const rules::IniFile& rules() const;
    const rules::IniFile& art_ini() const;
    const rules::IniFile& mode_catalog() const;
    const rules::IniFile& battle_rules() const;
    const std::string& battle_rules_name() const;
    const rules::IniFile& map_ini() const;
    const ra2map::MapFile& map() const;
    render::TheaterArt& theater();
    // Read an installed file using this loader's mounted base archives.
    std::optional<std::vector<std::uint8_t>> read_asset(const std::string& name) const;
    const PlacementGeometry& geometry() const;
    const std::vector<BaseMapCellInput>& cells_in_original_order() const;
    // Every nonempty packed byte, in the original 512-by-512 scan order.
    // Type eligibility and object construction have not yet run.
    const std::vector<BaseOverlayInput>& overlays_in_scan_order() const;
private:
    struct Data;
    explicit BaseBattleAssets(std::unique_ptr<Data>);
    std::unique_ptr<Data> data_;
};

// State immediately after the packed tile reader, before tubes and overlays.
// This is not a placement or live-runtime snapshot. Subsequent phases can
// change every cell, including bridge and occupancy fields.
struct PackedMapState {
    PlacementGeometry geometry;
    std::vector<std::uint32_t> cell_order;
    std::map<std::uint32_t,CellState> cells;
};
// The default live tile index is required only when an allocated cell has no
// packed record. It must come from the original theater/default-tile load.
std::optional<PackedMapState> read_base_packed_map(const BaseBattleAssets&,
    std::optional<std::uint32_t> default_live_tile);
// Builds a partial owning snapshot at the same boundary. The RNG must be the
// actual state at this boundary. Complete scenario setup is still required.
std::optional<Snapshot> packed_map_snapshot(const PackedMapState&,const sim::RandomState&);
// Apply the separate OverlayDataPack scan after real Overlay object loading.
// The input must already be at OverlayObjectsRead; this does not resolve the
// preceding Overlay eligibility, object placement, or bridge phases.
bool apply_base_overlay_data(Snapshot&,const BaseBattleAssets&);

// Base 005B7C74 writes the selected signed Cell center with world Z zero.
sim::Coord battle_start_position(StartPoint) noexcept;

struct InitialBaseUnitInput {
    std::uint32_t house_id;
    std::string type;
    std::int32_t maximum_hp, type_index;
    bool excluded_from_house_tracking;
    StartPoint start;
    // Exact House +53BC world coordinate supplied by the original start pass.
    sim::Coord start_position;
    BaseUnitProfile profile;
};
struct DirectBaseUnitResult {
    std::uint32_t unit_id;
    // A blocked direct placement must continue with the original radius search.
    PlacementResult placement;
};
// Base 005B7F90 constructs the Unit, then attempts its start position at
// facing zero. The supplied world and type fields must be from the real phase.
// This checked slice accepts cell centers with Z zero or the floor height.
// A successful direct placement also completes its Cell setup. Unknown inputs
// leave the owning snapshot and its full RNG state unchanged.
std::optional<DirectBaseUnitResult> place_direct_base_unit(Snapshot&,
    const InitialBaseUnitInput&, const sim::CellSetupStatic&, const sim::TheaterInputs&,
    std::uint8_t cliff, const std::map<std::int32_t,float>& land_wheels);

// Continue after a blocked direct attempt using the same created Unit and RNG.
// An exhausted search remains unresolved until Unit destruction is recovered.
// The caller supplies the original search bounds and resolved Cell guards.
std::optional<StartPoint> search_blocked_base_unit(Snapshot&,std::uint32_t unit_id,
    StartPoint origin,PlacementBounds,const BaseUnitProfile&,const sim::CellSetupStatic&,
    const sim::TheaterInputs&,std::uint8_t cliff,const std::map<std::int32_t,float>& land_wheels);

struct PlayerHouseSetup {
    std::string name, country;
    std::int32_t credits, selected_color;
    std::optional<std::int32_t> original_index;
};
// Runs the known constructor fragments in supplied order. It omits original
// calls between houses, including difficulty and palette processing. The RNG
// result is only the fragment result, not a complete house phase handoff.
// Use Stage for individual constructors when integrating the missing phases.
std::optional<Snapshot> construct_house_fragments(const sim::RandomState& before_houses,
    const std::vector<PlayerHouseSetup>& players, std::int32_t unit_registry_size,
    std::optional<std::int32_t> light_grey_palette,
    std::optional<std::int32_t> neutral_index={}, std::optional<std::int32_t> special_index={});

// A separate playable approximation. The caller supplies the random state and
// owns the returned world. PrototypeComplete must never enter the exact path.
struct PrototypePlayerSetup {
    std::string name, country;
    std::int32_t credits=10000, color=0;
    // -1 selects the next map starting waypoint in index order.
    std::int32_t waypoint_index=-1;
    // Unit Count: cost budget slots in addition to the starting MCV.
    std::int32_t vehicle_count=10;
    bool human=true;
};
struct PrototypeBattleSetup { std::vector<PrototypePlayerSetup> players; };
struct PrototypeBattle {
    Snapshot snapshot;
    std::vector<std::string> gaps;
};
// Loads authored map actors and player vehicles into one linked world. Rules,
// art, the map, and the mode come from assets. Gaps name remaining prototype
// behavior. The input random state is never changed by a failed call.
std::optional<PrototypeBattle> prototype_battle(BaseBattleAssets&,
    const PrototypeBattleSetup&,const sim::RandomState&);
}
