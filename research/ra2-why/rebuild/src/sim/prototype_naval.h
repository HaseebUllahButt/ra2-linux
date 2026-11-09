#pragma once

#include "../rules/ini_file.h"
#include "../session/world/world.h"
#include "prototype_movement.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim {

// Original SpeedType order: base 00482F20 name table; YRpp
// GeneralDefinitions.h agrees. Ships use Float; hover craft use Hover.
enum class NavalSpeedType : std::int32_t {
    None=-1, Foot=0, Track=1, Wheel=2, Hover=3, Winged=4, Float=5, Amphibious=6, FloatBeach=7
};

// Case-insensitive original SpeedType name lookup. Empty is unsupported text.
std::optional<NavalSpeedType> naval_speed_type(std::string_view name);

// Authored rules for one prototype ship type. Every value is read from the
// given base rules; nothing is defaulted or computed here.
struct PrototypeShipRules {
    std::string type;                    // DEST, DLPH, SQD, AEGIS, CARRIER, SUB, DRED.
    std::string display_name;            // Authored Name, for example "Destroyer".
    std::int32_t strength=0, cost=0, rule_speed=0, tech_level=0, sight=0;
    NavalSpeedType speed_type=NavalSpeedType::None; // Float for ships, Hover for hover craft.
    bool naval=false;                    // Naval=yes: the naval yard builds it.
    bool underwater=false;               // Underwater=yes: submerged, hidden without sensors.
    bool sensors=false;                  // Sensors=yes: this unit detects submerged units.
    std::int32_t sensors_sight=0;        // Sensors detection range in cells.
    std::string category, primary, armor;
    // Throws std::runtime_error for a missing section or invalid core values.
    static PrototypeShipRules load(const rules::IniFile& rules, std::string_view type);
};

// The seven base ships the prototype supports, in VehicleTypes order.
const std::vector<const char*>& prototype_ship_types();
// Load every supported ship. Throws when the base rules miss one.
std::vector<PrototypeShipRules> load_prototype_ship_rules(const rules::IniFile& rules);

// Movement cost percent per land type and speed type, read from the base
// rules land sections ([Clear], [Water], [Beach], and so on). The original
// keeps the same table in Ground[land].Cost[speed] (RA1 RulesClass::Movement
// in CnC_Red_Alert CODE/RULES.CPP) and VesselClass::Can_Enter_Cell rejects a
// zero cost; RA2 reads it through 0064D610.
class PrototypeWaterTable {
public:
    static PrototypeWaterTable load(const rules::IniFile& rules);
    // LandType index: Clear 0, Road 1, Water 2, Rock 3, Wall 4, Tiberium 5,
    // Beach 6, Rough 7, Ice 8, Railroad 9, Tunnel 10, Weeds 11 (0064D610).
    // Empty means the cell land type or speed is outside the table.
    std::optional<std::int32_t> cost(std::int32_t land_type, NavalSpeedType speed) const;
private:
    static constexpr std::size_t land_count=12, speed_count=8;
    std::array<std::array<std::optional<std::int32_t>,speed_count>,land_count> costs_{};
};

// Water cell access for the prototype. True open, false blocked, empty
// unresolved Cell state. Ships (Float) enter Water cells only; Hover craft
// also cross Beach and Clear cells at their table cost. Bridge cells and
// cells held by another object are blocked.
// TODO-ORIGINAL: simplified VesselClass::Can_Enter_Cell (RA1 CODE/VESSEL.CPP;
// RA2 ship path 006A05F0). No blocking terrain objects, no moving-vehicle
// pass-through, no mines.
std::optional<bool> naval_open_cell(const session::world::Snapshot&,
    ra2map::Cell cell, NavalSpeedType speed_type, const PrototypeWaterTable&,
    std::uint32_t self);

// Submerged visibility for the prototype. A placed enemy with
// Underwater=yes is visible to the viewer house only when one live entity
// of that house with Sensors=yes stands within its SensorsSight cells
// (Chebyshev distance). Own units, dead units and surface units stay
// visible. TODO-ORIGINAL: the original keeps per-house sight maps
// (Display_VisitNearbyOwnedOrAlliedTechnosWithinSightPlusRadius 004AE0B0),
// shares allied vision and reveals a submerged unit while it fires.
bool prototype_sees_ship(const session::world::Snapshot&, const rules::IniFile&,
    std::uint32_t viewer_house, std::uint32_t target);

// Naval shipyard factory. The base naval yards are GAYARD and NAYARD
// (General BuildNavalYard, read at 0064C2B0). A shipyard is a live
// Structure with Factory=UnitType, Naval=yes and WaterBound=yes; ships
// list the yard in Prerequisite, so they need it as their factory.
bool is_naval_yard(const rules::IniFile& rules, const session::world::Entity& yard);
// First live naval yard of the house, lowest entity id. Null without one.
const session::world::Entity* naval_yard(const session::world::Snapshot&,
    const rules::IniFile& rules, std::uint32_t house);
// Simple shipyard buildable check: Naval=yes unit type, Owner country,
// house tech level, live prerequisites, and cash.
// TODO-ORIGINAL: House::Can_Build 004E3660 and the production runtime stay
// responsible for the final legality check.
bool naval_buildable(const session::world::Snapshot&, const rules::IniFile&,
    std::uint32_t house, std::string_view type);
// Build one ship for the house at a free water Cell next to its naval yard
// and pay the cost. Returns the new entity id. Empty when the yard, rules,
// cash or a water dock cell is missing. The ship arrives placed and live on
// its cell. TODO-ORIGINAL: BuildingClass::Exit_Object 0045C930 uses the
// authored Dock cells; the prototype searches a ring around the yard.
std::optional<std::uint32_t> naval_produce(session::world::Snapshot&,
    const rules::IniFile& rules, const PrototypeWaterTable&,
    std::uint32_t house, std::string_view type);

// Playable naval movement. Water-only A* route for Float ships; Hover craft
// also cross Beach and Clear cells at their table cost. Same staged cell
// contract as PrototypeMovement: issue reads, tick publishes one private
// copy. Cell cost below 100 slows the step.
// TODO-ORIGINAL: replace grid routing and simple motion with the ship
// locomotor (ShipLocomotionClass 0069EC50, Move_To 0069F450, cell walk
// 006A05F0) and HoverLocomotionClass 00513C20.
class PrototypeNavalMovement {
public:
    // Speed is world units per frame before the cell cost scaling. The ship
    // speed type comes from its rules SpeedType key.
    PrototypeMoveResult issue(const session::world::Snapshot&, std::uint32_t unit,
        ra2map::Cell goal, int speed, NavalSpeedType, const PrototypeWaterTable&);
    void cancel(session::world::Snapshot&, std::uint32_t unit);
    bool moving(std::uint32_t unit) const { return moves_.count(unit)!=0; }
    std::size_t active_count() const { return moves_.size(); }
    // Apply all pending moves in ID order to one private world and publish it.
    PrototypeMoveResult tick(session::world::Snapshot&, std::uint32_t game_frame,
        const PrototypeWaterTable&);
private:
    struct Move { ra2map::Cell goal; int speed=0; NavalSpeedType speed_type=NavalSpeedType::None;
        std::vector<ra2map::Cell> path; };
    std::map<std::uint32_t,Move> moves_;
    std::optional<std::uint32_t> last_frame_;
};

} // namespace ra2::sim
