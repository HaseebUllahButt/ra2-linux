#pragma once
#include "../session/world/world.h"
#include <functional>
namespace ra2::sim {
enum class UnitVisibilityStatus { Complete, NeedsInput, Unsupported };
struct UnitVisibilityResult {
    UnitVisibilityStatus status=UnitVisibilityStatus::NeedsInput;
    std::optional<bool> first_visibility;
    bool seen_called=false;
    std::string detail;
};
struct UnitVisibilityInputs {
    // Divisor at base 00A6FC30. Use the original resolved runtime value.
    std::optional<std::int32_t> projection_divisor;
    std::optional<bool> global_mode;
    std::optional<bool> local_house_flag_13d,global_flag_a3d548;
    const std::vector<bool>* allocated=nullptr;
    // Original 005683C0. It is reached only when 00568140 returns false.
    std::function<std::optional<bool>(session::world::Snapshot&,Coord)> secondary;
    // Unit virtual +180 with the original local House, not a generic reveal.
    std::function<std::optional<bool>(session::world::Snapshot&,std::uint32_t)> seen;
};
// Base 005683C0. No value means a required global, Cell, or fallback is unknown.
std::optional<bool> original_secondary_unit_visibility(const session::world::Snapshot&,Coord,const UnitVisibilityInputs&);
// Visibility branch of base Cell insertion 00473B30 for a flat ground Unit.
// Missing Cells never imply an unobstructed projection. Only Complete publishes
// the caller's private Snapshot. Fallback Cell writes remain unresolved.
UnitVisibilityResult original_unit_insertion_visibility(session::world::Snapshot&,std::uint32_t unit,
    std::uint32_t cell,const UnitVisibilityInputs&);
}
