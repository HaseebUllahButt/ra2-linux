#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Original base RA2 (game.exe) production and building placement decisions.
// Every input is a resolved original value. An empty optional is unknown and
// makes the decision return NeedsInput. No input has a guessed default.
namespace ra2::sim::production {

// Original RTTI values returned by virtual +2C.
enum class Rtti : std::int32_t {
    Unit=1, Aircraft=2, AircraftType=3, Building=6, BuildingType=7,
    Infantry=0xF, InfantryType=0x10, Terrain=0x24, UnitType=0x28,
};

// ---------------------------------------------------------------------------
// House::Can_Build, base 004E3660.
enum class CanBuild { Cannot, Can, LimitReached, NeedsInput, Unsupported };

struct CanBuildType {
    Rtti rtti=Rtti::BuildingType;
    std::int32_t heap_index=0;            // Virtual +40 (BuildingType +B90).
    bool constructor_flag_aa4=false;      // TechnoType +AA4. Only 006D9xxx writes it (zero).
    std::vector<std::int32_t> prerequisite;          // +560 list.
    std::vector<std::int32_t> prerequisite_override; // +57C list.
    std::int32_t tech_level=0;            // +55C.
    bool requires_stolen_allied=false;    // +B4C.
    bool requires_stolen_soviet=false;    // +B4B.
    std::uint32_t required_houses=0xFFFFFFFF;  // +B50.
    std::uint32_t forbidden_houses=0xFFFFFFFF; // +B54.
    std::int32_t super_weapon=-1;         // BuildingType +1374.
    std::int32_t build_limit=0;           // +388.
    bool infantry_flag_c14=false;         // InfantryType +C14 adds matching objects.
};

// One house building, in original House +6C vector order.
struct HouseBuilding {
    bool limbo=false;                     // Object +76.
    bool flag_530=false;                  // Building +530.
    std::int32_t mission=0;               // Virtual +16C result.
    std::int32_t mission_a0=0;            // Object +A0.
    std::array<std::optional<std::int32_t>,3> upgrades{}; // +4C0..+4C8 type heap indexes; empty = null.
};

struct CanBuildHouse {
    std::optional<std::int32_t> tech_level;        // House +11C.
    std::optional<bool> stolen_allied, stolen_soviet; // House +205 and +204.
    std::optional<std::int32_t> country;           // HouseType +B8 through House +34.
    std::optional<std::array<std::uint32_t,4>> stolen_masks; // +208 infantry, +20C unit, +210 aircraft, +214 building.
    std::map<std::int32_t,bool> super_weapon_disabled; // House +1A0[i] -> type +24 -> +DF.
    std::optional<bool> flag_134, flag_135;        // House +134 and +135.
    // CounterClass arrays: absent indexes read as zero, as 00491D10 does after growth.
    std::optional<std::map<std::int32_t,std::int32_t>> active_buildings; // House +5460.
    std::optional<std::map<std::int32_t,std::int32_t>> owned_buildings, owned_units,
        owned_infantry, owned_aircraft;            // +5420, +5430, +5440, +5450.
    std::optional<std::map<std::int32_t,std::int32_t>> built_buildings, built_units,
        built_infantry, built_aircraft;            // +54A0, +54B0, +54C0, +54D0.
    std::optional<std::vector<HouseBuilding>> buildings; // House +6C list.
    // Infantry objects owned by this house whose +290 equals the type heap index.
    std::optional<std::int32_t> infantry_c14_matches;
    // Factories owned by this house, in global factory order. Each value is the
    // produced object's (RTTI, heap index), or empty when the factory has no object.
    std::optional<std::vector<std::optional<std::pair<Rtti,std::int32_t>>>> factory_objects;
};

struct CanBuildRules {
    std::optional<bool> super_weapons_allowed; // 00A3D2C3.
    std::optional<std::int32_t> session_type;  // 00A3D298.
    // Rules +2B8, +2D4, +2F0, +30C, +328, +344: Power, Factory, Barracks, Radar, Tech, Proc.
    std::optional<std::array<std::vector<std::int32_t>,6>> generic_prerequisites;
    // BuildingType registry facts used by default prerequisites (+C20 PowersUpBuilding nonempty).
    std::map<std::int32_t,bool> powers_up_building;
};

CanBuild original_can_build(const CanBuildType&, const CanBuildHouse&, const CanBuildRules&,
                            bool build_limit_only, bool include_queued);

// ---------------------------------------------------------------------------
// Placement: TechnoType::Legal_Placement 006DE0E0 and Cell::Is_Clear_To_Build 004718B0.
struct CellXY { std::int16_t x=0, y=0; bool operator==(const CellXY& o) const { return x==o.x && y==o.y; } };

// BuildingType +B94 lists, 0084EDE8 + Foundation*120, built by 00454EF0.
const std::vector<CellXY>& original_foundation_cells(std::int32_t foundation);

struct CellOccupant {
    Rtti rtti=Rtti::Unit;
    bool techno=false;                    // Abstract +14 bit 1.
};

struct PlacementCell {
    std::optional<std::vector<CellOccupant>> ground; // Cell +E4 chain, original order.
    std::optional<std::uint8_t> occupancy_124;       // Cell +124.
    std::optional<std::int8_t> height;               // Cell +11B.
    std::optional<std::uint8_t> ramp;                // Cell +11C.
    std::optional<std::int32_t> overlay;             // Cell +44, -1 is none.
    std::optional<std::uint32_t> flags_140;          // Cell +140.
    std::optional<std::int32_t> land_type;           // Cell +EC.
};

struct PlacementMap {
    std::int32_t width=0;                           // Map +F0.
    std::int32_t local_x=0, local_y=0, local_width=0, local_height=0; // Map +F8..+104.
    // Allocated cells by packed (y<<9)|x. Unallocated coordinates use the
    // 00548070 fallback cell; its state must be supplied when used.
    std::map<std::uint32_t,PlacementCell> cells;
    std::optional<PlacementCell> fallback;
    std::optional<std::array<bool,12>> buildable;   // LandType records at 00850EC0.
    std::optional<std::int32_t> scenario_init;      // 00A40774.
    std::optional<bool> object_scan;                // 00A40958.
    std::optional<bool> editor_mode;                // 00A40D13.
};

struct PlacementBuilding {
    std::int32_t foundation=0;          // BuildingType +C78.
    std::int32_t speed_type=-1;         // TechnoType +5A4.
    bool place_anywhere=false;          // +1387.
    bool naval=false;                   // +ADA.
    bool laser_fence=false, laser_fence_post=false, gate=false; // +1359, +1358, +1351.
    bool to_tile=false;                 // +BF0 nonzero.
    bool to_overlay=false;              // +BEC nonzero.
};

enum class Placement { Legal, Illegal, NeedsInput, Unsupported };
std::uint32_t packed_cell(CellXY);
// 004718B0 for a building type and speed type -1, outside special fence/tile/wall/naval branches.
Placement original_clear_to_build(const PlacementMap&, CellXY, const PlacementBuilding&);
// 0045D880 then 006DE0E0 for a BuildingType.
Placement original_legal_placement(const PlacementMap&, CellXY origin, const PlacementBuilding&);
// 0045EC50/0045EC90 deploy helpers: true when the cell is not offset by 00851A8C.
bool original_deploy_without_offset(bool sensor_array, bool tick_tank, bool icbm, bool artillery);
std::uint8_t original_deploy_facing(bool sensor_array, bool tick_tank, bool icbm, bool artillery);
}
