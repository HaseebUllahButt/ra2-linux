#pragma once
#include <optional>
#include <string>
namespace ra2::weapons {
// Input fields are resolved base 006C9260 state and virtual query results.
// Do not supply values inferred from a missing actor or cell.
struct FireEligibilityPrefixInput {
    std::optional<bool> target_present, shooter_virtual_1c0, shooter_virtual_1bc;
    std::optional<bool> shooter_flag_325, shooter_virtual_1c4, attachment_present, target_is_attachment;
    std::optional<bool> target_is_object, target_cell_resolved;
    std::optional<bool> target_limbo, target_flag_210;
    std::optional<int> target_virtual_64;
    std::optional<bool> target_cell_visible;
    std::optional<int> averaged_weapon_damage;
    std::optional<bool> target_house_allied;
    std::optional<bool> shooter_flag_7a, shooter_virtual_324;
    std::optional<int> shooter_original_kind;
    std::optional<bool> unit_type_bb0, unit_type_bb1;
};
struct FireEligibilityPrefixResult {
    // 3, 5, and 6 are original rejection codes. Continue is only a prefix.
    std::optional<int> rejection;
    bool continued=false;
    std::string missing;
};
FireEligibilityPrefixResult base_fire_eligibility_prefix(const FireEligibilityPrefixInput&);
}

#include "fire_timer.h"
namespace ra2::weapons {
// Guarded continuation of the Unit virtual +364 check. All optionals are
// resolved original actor, weapon, warhead, and cell values. The two *_clear
// fields mean every listed original flag is explicitly known to be zero.
struct OrdinaryUnitFireInput {
    FireEligibilityPrefixInput prefix;
    std::optional<bool> shooter_is_unit, target_is_unit, weapon_present, primary_weapon_index_zero;
    // Weapon +125, +10D, +105, +106, +109, +10C, +10F are all zero.
    std::optional<bool> weapon_special_flags_clear;
    // The original separately reads slot 1 and checks its four effect flags
    // against live effect pointers, even when slot 0 is selected.
    std::optional<bool> alternate_weapon_effects_clear;
    // Warhead +153, +154, +156, +158 are all zero.
    std::optional<bool> warhead_special_flags_clear, warhead_present;
    std::optional<bool> target_virtual_1bc, warhead_flag_157;
    std::optional<bool> target_virtual_54;
    std::optional<int> target_cell_land_type;
    std::optional<bool> target_virtual_50;
    std::optional<int> shooter_type_54c;
    // False means the Unit barrel/burst override did not replace the timer.
    std::optional<bool> barrel_override_active;
    std::optional<FireTimer> timer;
    std::optional<int> frame, ammo_field_254;
    std::optional<bool> shooter_type_af2;
    std::optional<double> warhead_armor_verse;
    std::optional<bool> target_flag_325, matching_bridge_flags;
    std::optional<bool> shooter_type_b47, shooter_virtual_328;
};
// A result code of zero means the original +364 virtual check passed in this
// guarded slice. It does not select a target, turn the turret, test range, or
// execute a shot. Missing or exceptional state returns no code.
FireEligibilityPrefixResult base_ordinary_unit_fire_check(const OrdinaryUnitFireInput&);
}
