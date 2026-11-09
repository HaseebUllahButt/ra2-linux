#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ra2::session::world {
enum class SpeedKeyState { Unknown, Absent, Present };
enum class SpeedTypeStatus { Complete, NeedsInput, Unsupported };
struct BaseSpeedTypeInput {
    std::optional<std::int32_t> initial_type;
    std::optional<bool> crusher;
    SpeedKeyState key_state=SpeedKeyState::Unknown;
    std::string key_value;
};
struct BaseSpeedTypeResult {
    SpeedTypeStatus status=SpeedTypeStatus::NeedsInput;
    std::optional<std::int32_t> index;
};
// Base UnitType 0070ABEA and 0046C400. This checked slice accepts Track
// and Wheel only. The caller resolves constructor, INI overlays, and Crusher.
// Present key_value is the parsed and trimmed original INI string.
BaseSpeedTypeResult resolve_base_speed_type(const BaseSpeedTypeInput& input);
}
