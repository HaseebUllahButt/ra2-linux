#pragma once
#include <array>
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "../rules/ini_file.h"
#include "sim.h"

namespace ra2::session::world { struct Snapshot; }

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
// DisplayClass::Passes_Proximity_Check 004991D0 with resolved Cell contents.
struct ProximityBuilding {
    std::int32_t owner_index=0;          // Owner House +30.
    bool base_normal=true, eligible_ally=false; // Type +1203 / +1204.
    bool allied=false;                   // 004E5540(owner -> placing house).
};
struct ProximityInputs {
    std::int32_t house_index=0, player_index=0; // Argument and PlayerPtr +30.
    bool editor_mode=false;              // 00A40D13.
    bool pending=true;                   // Argument 3 nonzero.
    bool building_type=true;             // Pending type RTTI is 7.
    std::int32_t foundation=0, adjacent=0;
    CellXY cell;
    std::optional<bool> build_off_ally;  // 00A3D2C4.
    // First Building in each canonical Cell chain (004717B0). Missing keys use the empty fallback.
    std::map<std::uint32_t,std::optional<ProximityBuilding>> buildings;
    bool object_scan=true;               // 00A40958.
};
std::optional<bool> original_passes_proximity(const ProximityInputs&);
// 0045EC50/0045EC90 deploy helpers: true when the cell is not offset by 00851A8C.
bool original_deploy_without_offset(bool sensor_array, bool tick_tank, bool icbm, bool artillery);
std::uint8_t original_deploy_facing(bool sensor_array, bool tick_tank, bool icbm, bool artillery);

// ---------------------------------------------------------------------------
// Resolved rules. Each field starts from the executed original constructor
// value (BuildingType 00457AC0 with TechnoType 006D9B40), then applies the
// original reader order: rules [ID], art [Image] then art [ID] for Foundation.
struct TypeFacts {
    std::string id;
    Rtti rtti=Rtti::BuildingType;
    std::int32_t heap_index=0;
    CanBuildType can;
    std::uint32_t owner=0;               // +5E8 (00482DE0 bits by [Countries] index).
    bool double_owned=false;             // +AA5.
    std::int32_t cost=0;                 // +550.
    std::int32_t strength=0;             // ObjectType +A0.
    std::string deploys_into;            // UnitType +3C8 name.
    // BuildingType.
    PlacementBuilding placement;
    std::int32_t adjacent=3;             // +C40.
    bool base_normal=true;               // +1203.
    bool eligible_ally=false;            // +1204.
    std::optional<Rtti> factory;         // +C44.
    bool construction_yard=false;        // +1353.
    bool powers_up=false;                // +C20 nonempty.
    std::string free_unit;               // +C38.
    std::int32_t build_cat=0;            // +BA0.
    std::int32_t power_output=0, power_drain=0; // +C68/+C6C from Power.
    std::int32_t extra_output=0, extra_drain=0; // +C70/+C74 from ExtraPower.
    bool sensor_array=false, tick_tank=false, icbm=false, artillery=false; // +1362 +135E +1363 +1364.
    bool wall=false;                     // +1215.
};

struct Catalog {
    std::vector<std::string> countries;  // HouseType registry, [Countries] order.
    std::vector<TypeFacts> buildings, units, infantry, aircraft; // Heap order.
    std::array<std::vector<std::int32_t>,6> generic_prerequisites; // Rules +2B8.. lists.
    std::optional<std::array<bool,12>> buildable;                  // LandType Buildable.
    double build_speed=0;                 // Rules +13B0.
    float minimum_low_power=0, maximum_low_power=0, low_power_penalty=0, multiple_factory=0;
    double wall_coefficient=0;
    std::vector<std::string> pad_aircraft; bool separate_aircraft=false;
    std::vector<std::string> gaps;        // Inputs the reader could not resolve.
    static Catalog from(const rules::IniFile& rules,const rules::IniFile& art,const rules::IniFile& map);
    const TypeFacts* find(const std::string& id) const;
    const TypeFacts* building(std::int32_t heap) const;
};

// Original 54-stage factory (FactoryClass, 0x70 bytes, global order 00A35E94).
struct FactoryState {
    std::uint32_t house_id=0;
    std::int32_t slot=0;                 // House +52D8 aircraft, +52DC infantry, +52E0 unit, +52E4 naval, +52E8 building, +52F8 defense.
    std::uint32_t object=0;              // +54 entity id; zero is null.
    std::string object_type;
    std::int32_t progress=0;             // +24.
    std::int32_t balance=0;              // +5C.
    std::int32_t interval=0;             // +34.
    std::uint64_t timer_start=0;         // +28.
    bool suspended=true;                 // +6C.
};
struct SessionFlags {
    std::optional<std::int32_t> session_type;  // 00A3D298.
    std::optional<bool> super_weapons;         // 00A3D2C3.
    std::optional<bool> build_off_ally;        // 00A3D2C4.
    std::optional<std::uint32_t> player_house; // 00A35DB4.
};
// Building fields the shared Entity does not hold yet.
struct BuildingState {
    bool on_530=true;            // Building +530, set by constructor 00438E1C.
    bool primary=false;          // Building +32B, cleared by Unlimbo 0043E03C.
    std::int32_t mission_a0=-1;  // Object +A0, reset by Set_Mission 00594710.
};
struct FactorySet {
    SessionFlags flags;
    std::map<std::uint32_t,BuildingState> buildings;
    std::vector<FactoryState> factories;                 // Original global order.
    std::map<std::uint32_t,std::array<std::int32_t,6>> house_factory_counts; // +52B8..+52C8 by slot kind.
    std::map<std::uint32_t,bool> power_dirty;            // House +5668.
    // House +1EC/+1F4 power blackout timer: true when the timer is idle and zero.
    std::map<std::uint32_t,bool> power_blackout_idle;
};

enum class Status { Done, Waiting, NeedsTurn, Rejected, NeedsInput, Unsupported };
struct Outcome {
    Status status=Status::NeedsInput;
    std::string detail;
    std::optional<std::uint32_t> entity;
    std::optional<std::int32_t> value;
};

// Original effects not yet proven against x86. A command that reaches one
// calls the hook. A missing or failed hook returns NeedsInput and the command
// publishes nothing.
struct EffectHooks {
    // Foot Mark up/down after the chain and bit 0x20 writes: Cell setup and visibility.
    std::function<bool(session::world::Snapshot&,std::uint32_t unit,bool down)> unit_mark;
    // Constructor fields of a new object beyond id, RNG word, owner, type and limbo state.
    std::function<bool(session::world::Snapshot&,std::uint32_t entity)> constructor;
    // BuildingClass::Unlimbo 0043D3A0 effects beyond links, counters, drain and factory count.
    std::function<bool(session::world::Snapshot&,std::uint32_t building)> building_unlimbo;
    // Unit Limbo and delete callbacks beyond lifetime and counters.
    std::function<bool(session::world::Snapshot&,std::uint32_t unit)> unit_removal;
    // Optional: turn a Unit to the deploy facing now. Without it deploy returns NeedsTurn
    // and the movement owner must turn the Unit (locomotor Do_Turn at 006FE16B).
    std::function<bool(session::world::Snapshot&,std::uint32_t unit,std::uint8_t facing)> turn;
    // Optional: move a completed Unit or Infantry out of its factory. Without it the
    // object waits in the factory. Original: House::Place_Object 004E6950 cell -1 path
    // and BuildingClass::Exit_Object 00440520.
    std::function<bool(session::world::Snapshot&,std::uint32_t object,std::uint32_t factory_building)> exit_object;
};
// PLAYABLE PROTOTYPE. Placeholder hooks so a skirmish can run before the
// original effects are recovered. Each hook is marked TODO-ORIGINAL in the source.
EffectHooks prototype_hooks();

// Every command copies Snapshot and FactorySet, then publishes both only on success.
// UnitClass::Try_To_Deploy 006FDFC0 for a resolved, stationary Unit.
// can_deploy is the resolved result of the Unit's virtual +2E8 (006CD340),
// whose tube-cell and locomotor branches are not recovered here.
Outcome deploy(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t unit_id,std::optional<bool> can_deploy,
    const EffectHooks&);
// Sidebar eligibility, House::Can_Build(type,false,true) for every type.
std::vector<std::pair<std::string,CanBuild>> options(const session::world::Snapshot&,const FactorySet&,
    const Catalog&,std::uint32_t house_id);
// Event PRODUCE: House::Begin_Production 004E5E30.
Outcome begin_production(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,std::uint64_t frame,const EffectHooks&);
// DisplayClass::Passes_Proximity_Check 004991D0; the UI calls it before PLACE.
std::optional<bool> passes_proximity(const session::world::Snapshot&,const FactorySet&,const Catalog&,
    std::uint32_t house_id,const std::string& type,CellXY);
// Event PLACE with a cell: House::Place_Object 004E6950 building path.
Outcome place(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint32_t house_id,
    const std::string& type,CellXY,std::uint64_t frame,const EffectHooks&);
// Logic before events: House::AI power recalculation 004F2CC0, then FactoryClass::AI 004B9300.
// Completed Units and Infantry then leave through hooks.exit_object.
Outcome tick(session::world::Snapshot&,FactorySet&,const Catalog&,std::uint64_t frame,const EffectHooks&);
// PLAYABLE PROTOTYPE for computer houses: nearest legal and adjacent origin
// around an anchor. TODO-ORIGINAL 004E9660/004EF890 AI base planning.
std::optional<CellXY> prototype_find_placement(const session::world::Snapshot&,const FactorySet&,const Catalog&,
    std::uint32_t house_id,const std::string& type,CellXY anchor,int radius);
}
