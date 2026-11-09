#pragma once
#include "sim/sim.h"
#include "sim/coarse_category.h"
#include "weapons/occupant_order.h"
#include "../placement_terrain.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ra2::session::world {
enum class Edition { Base, Yuri };
enum class Kind { Unit, Infantry, Aircraft, Structure, Terrain };
enum class Lifetime { Created, Placed, Destroyed };
enum class Layer { Ground, Bridge };
enum class CreatedKind { House, Entity };
enum class HouseRole { Player, Neutral, Special };
struct CreatedObject { CreatedKind kind; std::uint32_t id; };
struct House {
    std::uint32_t id=0;
    std::optional<std::int32_t> original_index;
    std::optional<HouseRole> role;
    std::string name, country;
    std::optional<std::int32_t> initial_credits, cash, selected_color, palette_color;
    std::optional<std::int32_t> constructor_random, unit_registry_size;
    std::optional<bool> category_flag; // Base House +142.
    std::optional<std::int32_t> unit_count; // Base House +228.
    std::optional<std::map<std::int32_t,std::int32_t>> unit_type_counts, unit_fixed_counts;
    std::optional<std::map<std::int32_t,std::int32_t>> placed_unit_counts;
    bool constructor_complete=false;
};
struct BaseUnitProfile {
    std::optional<std::int32_t> restricted_land, movement_mode, placement_mode, speed_type, level_height;
    std::optional<bool> type_flag_aa0, unit_flag_32c, linked_team;
    std::optional<bool> type_flag_205, unit_flag_32a, type_flag_bb0, type_flag_bb1;
};
struct Entity {
    std::uint32_t id=0, house_id=0; // Terrain may have no house (zero).
    std::optional<std::uint32_t> next; // Zero is null; no value is unresolved.
    Kind kind=Kind::Unit;
    Lifetime lifetime=Lifetime::Created;
    std::string type;
    std::optional<sim::Coord> position;
    std::optional<ra2map::Cell> cell;
    std::optional<std::int32_t> resolved_hp, hull_facing, turret_facing;
    std::optional<double> threat_field_428, resolved_threat;
    std::optional<std::int32_t> original_type_index;
    std::optional<bool> excluded_from_house_tracking;
    std::optional<std::uint16_t> constructor_random; // Base Techno +320.
    std::optional<bool> on_map;
    std::optional<bool> occupancy_tracking; // Base Foot +5A0.
    std::optional<std::int32_t> timer_length;
    std::optional<bool> timer_active;
    std::optional<std::uint8_t> height_byte;
    std::optional<Layer> layer;
    std::optional<sim::CoarseOccupantState> category;
    std::optional<BaseUnitProfile> base_profile;
    bool constructor_complete=false;
};
struct CellState {
    // Empty optional means occupancy is unresolved, not an empty cell.
    std::optional<weapons::CellContentHeads> heads;
    std::optional<sim::TerrainCell> terrain;
    std::optional<sim::CoarseOverlayState> overlay;
    std::optional<float> land_track;
    std::optional<bool> usable;
    std::optional<std::uint8_t> coarse_category, neighbor_count;
    // Base 004710B5 clears this byte. Unit entry 00703087 reads its bits.
    std::optional<std::uint8_t> ground_occupancy_124;
    std::optional<bool> cell_flag_100;   // Base Cell +140 bit 100.
    std::optional<bool> cell_flag_40000; // Base Cell +140 bit 40000.
    std::optional<bool> cell_flag_200;   // Separate Cell +140 bit 200.
    std::optional<float> unit_land_speed; // Resolved original locomotor land-table value.
    // Base Cell +54/+58 and signed +116. Constructors set -1, but later
    // state is not proved. Only +116 is known to index the TubeClass vector.
    std::optional<std::int32_t> cell_field_54, cell_field_58;
    std::optional<std::int16_t> tube_field_116;
};
// An owning snapshot. IDs use zero as null. Original creation order does not
// depend on the storage order of houses and entities. The shared next field is
// the original Object +30, including for multi-cell objects.
struct Snapshot {
    Edition edition=Edition::Base;
    // A placed Unit reached Cell setup 00472540, whose full effects are not
    // yet staged. Do not use this snapshot for further placement or live AI.
    bool pending_cell_setup=false;
    // Packed Cell index when known; a pending setup with no index is unresolved.
    std::optional<std::uint32_t> pending_setup_cell;
    std::vector<CreatedObject> creation_order;
    std::vector<House> houses;
    std::vector<Entity> entities;
    std::map<std::uint32_t,CellState> cells;
    std::optional<PlacementGeometry> geometry;
    std::optional<std::int32_t> session_field_838;
    std::optional<std::int32_t> tube_vector_count;
    sim::RandomState random;
};
enum class PlacementResult { Placed, Blocked, Unresolved };
// Checks every head and shared next chain. Invalid and cyclic snapshots fail.
bool validate(const Snapshot& snapshot);
// Preserve the original ground-chain order and all unknown phase inputs.
std::optional<sim::CoarseCategoryInputs> coarse_inputs(const Snapshot&, std::uint32_t cell);
// Base 006C4610 writes this flag before calling Object unlimbo. It uses the
// local map boundary, including cell height and ramp, even on a failed call.
std::optional<bool> base_unit_on_map(const PlacementTerrain&, StartPoint);

// Owns staged objects and the shared random state. House and Unit records hold
// only recovered fields; unresolved constructor fields remain optional.
class Stage {
public:
    explicit Stage(const sim::RandomState& random);
    explicit Stage(Snapshot snapshot);
    std::optional<std::uint32_t> create_player_house(std::string name, std::string country,
        std::int32_t credits, std::int32_t selected_color, std::int32_t unit_registry_size,
        std::optional<std::int32_t> original_index={});
    // Pass the LightGrey palette index only after its original lookup resolves.
    std::optional<std::uint32_t> create_auxiliary_house(HouseRole role,
        std::int32_t unit_registry_size, std::optional<std::int32_t> resolved_palette_color,
        std::optional<std::int32_t> original_index={});
    // Type index and +202 flag are resolved original UnitType values.
    std::optional<std::uint32_t> create_unit(std::uint32_t house_id, std::string type,
        std::optional<std::int32_t> resolved_hp, std::int32_t original_type_index,
        bool excluded_from_house_tracking);
    // Input cells are already resolved by the original second map pass.
    // Missing cells, occupancy, and runtime fields remain unknown.
    bool install_base_cells(PlacementGeometry geometry, std::map<std::uint32_t,CellState> cells,
        std::optional<std::int32_t> tube_vector_count, std::optional<std::int32_t> session_field_838);
    PlacementResult place_base_unit(std::uint32_t unit_id, StartPoint cell,
        std::uint8_t facing, const BaseUnitProfile& profile);
    const Snapshot& snapshot() const noexcept { return snapshot_; }
private:
    std::optional<std::uint32_t> next_id() const;
    Snapshot snapshot_;
};
}
