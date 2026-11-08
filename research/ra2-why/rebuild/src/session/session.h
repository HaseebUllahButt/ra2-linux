#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ra2::sim { class Random; class OriginalDistance; }
namespace ra2::session {
// The multiplayer house constructor copies these values before later setup.
struct HouseValues { std::int32_t initial_credits, cash, color, house_type_color; };
HouseValues initialize_house_values(std::int32_t credits, std::int32_t color) noexcept;
// Both editions create these houses after the player houses. The color is a
// named original color-scheme lookup, not a palette index.
struct AuxiliaryHouseType { const char* country; const char* color; std::int32_t color_argument; };
std::array<AuxiliaryHouseType,2> auxiliary_house_types() noexcept;
// Original multiplayer setup maps the selected color to a palette index.
std::int32_t multiplayer_house_color(std::int32_t selected_color) noexcept;
// Candidate order follows the resolved General/BaseUnit list. An unknown Owner
// mask blocks selection because it could be the first matching candidate.
struct BaseUnitCandidate { std::string type; std::optional<std::uint32_t> owner_mask; };
std::optional<std::size_t> select_base_unit(const std::vector<BaseUnitCandidate>& candidates,
                                            unsigned country_index);
// Fields are resolved original type values, including the original Cost call.
// Unknown fields block the result only when they are needed by the filter.
struct StartingUnitType {
    std::optional<bool> allowed;
    std::optional<std::uint32_t> owner_mask;
    std::optional<std::int32_t> tech_level, cost;
    bool vehicle=false;
    std::optional<bool> base_unit;
};
std::optional<std::int32_t> starting_unit_budget(std::int32_t unit_count,
    std::int32_t tech_level, std::uint32_t house_mask,
    const std::vector<StartingUnitType>& types);
// The candidate lists must already be filtered for the current house.
enum class StartingUnitGroup { Vehicle, Infantry };
struct StartingUnitChoice { StartingUnitGroup group; std::size_t index; };
std::optional<StartingUnitChoice> choose_starting_unit(std::int32_t initial_budget,
    std::int32_t remaining_budget, std::size_t vehicle_count, std::size_t infantry_count,
    sim::Random& random);
// Original waypoint cell coordinates are signed 16-bit values.
struct StartPoint { std::int16_t x, y; };
struct UnitAttemptResult { bool placed; StartPoint cell; };
struct StartingUnit { std::size_t type_index; StartPoint cell; };
enum class StartingMission { Guard=5, AreaGuard=11 };
StartingMission starting_unit_mission(bool human) noexcept;
// The successful Unit unlimbo suffix, after the parent placement call. Field
// meanings beyond their original byte offsets remain unresolved.
struct UnitUnlimboSuffix {
    std::uint16_t facing_value;
    std::optional<std::int32_t> status;
    std::int32_t timer_length;
    bool timer_active;
};
UnitUnlimboSuffix unit_unlimbo_suffix(std::uint8_t facing, bool unit_flag,
    bool unit_suppress_flag, bool type_flag_a, bool type_flag_b, sim::Random& random);
struct StartingUnitRun {
    std::vector<StartingUnit> placed;
    std::int32_t remaining_budget;
    unsigned failed_placements;
};
// The callback must use private scratch state. nullopt means its inputs are
// unresolved. A failed or unresolved plan must not be applied to the world.
using UnitAttempt = std::function<std::optional<UnitAttemptResult>(std::size_t, sim::Random&)>;
std::optional<StartingUnitRun> generate_starting_units(std::int32_t budget,
    std::int32_t house_tech_level, std::uint32_t house_bit,
    const std::vector<StartingUnitType>& types, sim::Random& random, const UnitAttempt& attempt);
// Call once after all houses complete unit generation successfully.
std::uint16_t finish_starting_unit_generation(sim::Random& random);
// Inclusive cell bounds and resolved map and object checks. The validation
// callback runs for every Yuri retry, including the origin. Base runs it only
// for the initial point. The placement callback resolves occupancy and unit
// placement. Both may consume the same RNG. Use only when checks are resolved.
struct PlacementBounds { std::int16_t left, top, right, bottom; };
enum class PlacementEdition { Base, Yuri };
// Original map Size and LocalSize use x, y, width, height.
struct MapRectangle { std::int32_t x, y, width, height; };
struct PlacementGeometry { std::int32_t width, height; MapRectangle local; };
// Returns no geometry for unsupported or empty map dimensions.
std::optional<PlacementGeometry> resolve_placement_geometry(MapRectangle size, MapRectangle local);
// height and ramp are the resolved original Cell +11B/+11C. Yuri needs both.
struct PlacementCell { std::int8_t height; std::uint8_t ramp; };
std::optional<bool> placement_cell_in_map(StartPoint cell, PlacementEdition edition,
    const PlacementGeometry& geometry, std::optional<PlacementCell> resolved_cell);
using PlacementCheck = std::function<bool(StartPoint, sim::Random&)>;
// A null result means unresolved input, not a blocked cell. The callback must
// use private scratch world state. On an unresolved result, RNG is unchanged.
using ResolvedPlacementCheck = std::function<std::optional<bool>(StartPoint, sim::Random&)>;
struct PlacementSearchResult { std::optional<StartPoint> point; };
std::optional<PlacementSearchResult> find_initial_placement_checked(StartPoint origin,
    PlacementBounds bounds, std::int32_t start_radius, PlacementEdition edition,
    sim::Random& random, const ResolvedPlacementCheck& valid, const ResolvedPlacementCheck& place);
std::optional<StartPoint> find_initial_placement(StartPoint origin, PlacementBounds bounds,
    PlacementEdition edition, sim::Random& random, const PlacementCheck& valid, const PlacementCheck& place);
// The extra-unit generator starts at radius 4. Negative radii are unsupported.
std::optional<StartPoint> find_initial_placement(StartPoint origin, PlacementBounds bounds,
    std::int32_t start_radius, PlacementEdition edition, sim::Random& random,
    const PlacementCheck& valid, const PlacementCheck& place);
// Returns a point index and marks it used. The RNG is the simulation's shared
// original state. Invalid or unsupported input leaves both arguments unchanged.
// This covers regular players in standard Battle mode, without observers.
// It is not a complete skirmish initialization plan.
std::optional<std::size_t> choose_start_point(const std::vector<StartPoint>& points,
    std::vector<bool>& used, sim::Random& random,
    const sim::OriginalDistance& distance);

// Inputs are resolved immediately after the first original start assignment.
// The supplied RNG is at that same point. Slots must be in original order:
// local slot 0, then AI slots. Observers are not supported.
struct BattleHouseInput {
    unsigned slot, country_index;
    std::uint32_t unit_owner_bit;
    // Difficulty is resolved engine level 0..2. The local player uses 1.
    std::int32_t raw_color, tech_level, difficulty;
};
struct BattlePlanInput {
    std::int32_t credits, unit_count, global_tech_level;
    bool bases_enabled;
    std::vector<BattleHouseInput> houses;
    std::vector<StartPoint> assigned_starts;
    std::vector<BaseUnitCandidate> base_units;
    std::vector<StartingUnitType> starting_types;
};
struct BattleHousePlan {
    BattleHouseInput input;
    HouseValues initial_values;
    StartPoint start;
    std::int32_t cash_before_units, cash_after_units, cash_after_world;
    StartingMission extra_unit_mission;
    std::optional<StartingUnit> base_unit;
    std::vector<StartingUnit> extra_units;
    // Render color after the original selected-color field copy.
    std::int32_t palette_color=0;
};
struct BattlePlan { std::vector<BattleHousePlan> houses; std::int32_t unit_budget; };
// World preparation includes map and terrain steps and the second original
// waypoint pass. It confirms the active slots, starts, and cash before units.
// A mismatch or nullopt blocks the plan.
struct PreparedBattleWorld {
    std::vector<unsigned> active_slots;
    std::vector<StartPoint> starts;
    std::vector<std::int32_t> cash;
    // Required after each world phase; confirms the resolved mode and type
    // inputs still match the staged world.
    bool resolved_types_unchanged=false;
    // Required after the final world phase; it confirms the staged units remain.
    bool staged_units_unchanged=false;
};
using PrepareBattleWorld = std::function<std::optional<PreparedBattleWorld>(
    const std::vector<BattleHousePlan>&, sim::Random&)>;
// base_unit selects the BaseUnit list; otherwise index selects starting_types.
// The callback handles the original direct placement and radius search. It
// must return nullopt if the supplied type values cease to be valid.
// All callbacks use private scratch state and return nullopt if unresolved.
using PlaceBattleUnit = std::function<std::optional<UnitAttemptResult>(
    std::size_t house, bool base_unit, std::size_t index, StartPoint start, sim::Random&)>;
// Resolves the four original per-house updates after unit generation. Return
// nullopt if they invalidate type values needed by a later house.
using FinishBattleHouse = std::function<std::optional<std::int32_t>(
    std::size_t house, std::int32_t cash, sim::Random&)>;
// Resolves remaining scenario initialization after the final unit RNG draw.
using FinishBattleWorld = PrepareBattleWorld;
std::optional<BattlePlan> plan_battle(const BattlePlanInput& input, sim::Random& random,
    const PrepareBattleWorld& prepare_world, const PlaceBattleUnit& place_unit,
    const FinishBattleHouse& finish_house, const FinishBattleWorld& finish_world);
}
