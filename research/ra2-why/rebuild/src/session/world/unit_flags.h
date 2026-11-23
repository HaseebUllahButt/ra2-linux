#pragma once
#include <optional>
#include <string>

namespace ra2::session::world {
enum class UnitFlagKeyState { Unknown, Absent, Present };
enum class UnitFlagStatus { Complete, NeedsInput, Unsupported };
struct UnitFlagKey {
    UnitFlagKeyState state=UnitFlagKeyState::Unknown;
    std::string value; // Parsed original INI value.
};
struct BaseUnitFlagInput {
    std::optional<bool> initial_is_train, initial_crusher;
    UnitFlagKey is_train, crusher;
    std::optional<bool> initial_small_visceroid={}, initial_large_visceroid={};
    UnitFlagKey small_visceroid={}, large_visceroid={};
};
struct UnitFlagResult {
    UnitFlagStatus status=UnitFlagStatus::NeedsInput;
    std::optional<bool> value;
};
struct BaseUnitFlagResults { UnitFlagResult is_train, crusher, small_visceroid, large_visceroid; };
// Defaults at the checked UnitType constructor return only. Later INI and
// type-loading writes can change these fields.
struct BaseUnitTypeConstructorFields {
    bool raw_205, is_train, crusher, raw_bb0, raw_bb1;
    int speed_type, movement_zone, restricted_land;
};
BaseUnitTypeConstructorFields base_unit_type_constructor_fields();
// Base 006DB03A and 006DD104 call boolean reader 0050F410 separately.
// Only exact yes/no, true/false, and 1/0 values are supported here.
BaseUnitFlagResults resolve_base_unit_flags(const BaseUnitFlagInput& input);
struct RestrictedLandResult {
    UnitFlagStatus status=UnitFlagStatus::NeedsInput;
    std::optional<int> value;
};
// Base 0070AD45 and 0046A970. Only the constructor's -1 default,
// the explicit <none> value, and Water are checked.
RestrictedLandResult resolve_base_restricted_land(std::optional<int> initial, const UnitFlagKey& key);
}
