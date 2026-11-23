#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace ra2::session::world {
enum class ZoneKeyState { Unknown, Absent, Present };
enum class ZoneStatus { Complete, NeedsInput, Unsupported };
struct BaseMovementZoneInput {
    std::optional<std::int32_t> initial_zone;
    ZoneKeyState key_state=ZoneKeyState::Unknown;
    std::string key_value; // Parsed and trimmed original INI value.
};
struct BaseMovementZoneResult {
    ZoneStatus status=ZoneStatus::NeedsInput;
    std::optional<std::int32_t> zone;
    std::optional<bool> raw_flag_af5;
};
// Base 006DE001 and 0046A300. Only Normal and Subterannean are checked.
// The caller resolves constructor state and all applicable INI overlays.
BaseMovementZoneResult resolve_base_movement_zone(const BaseMovementZoneInput& input);
}
