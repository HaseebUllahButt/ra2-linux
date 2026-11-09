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
