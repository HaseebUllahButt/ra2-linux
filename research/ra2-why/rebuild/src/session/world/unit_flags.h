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
};
struct UnitFlagResult {
    UnitFlagStatus status=UnitFlagStatus::NeedsInput;
    std::optional<bool> value;
};
struct BaseUnitFlagResults { UnitFlagResult is_train, crusher; };
// Defaults at the checked UnitType constructor boundary only. Later INI and
// type-loading writes can change these fields.
struct BaseUnitTypeConstructorFields {
    bool raw_205, is_train, crusher, raw_bb0, raw_bb1;
    int speed_type, movement_zone;
};
BaseUnitTypeConstructorFields base_unit_type_constructor_fields();
// Base 006DB03A and 006DD104 call boolean reader 0050F410 separately.
// Only exact yes/no, true/false, and 1/0 values are supported here.
BaseUnitFlagResults resolve_base_unit_flags(const BaseUnitFlagInput& input);
}
