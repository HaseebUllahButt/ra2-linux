#pragma once
#include <sim/distance.h>
#include <optional>
namespace ra2::weapons {
// Base 00469AE0 conversion after the original INI reader returns a double.
// -1 keeps the caller's constructor or previous value. Unsupported numeric
// conversions return empty; no rule defaults are supplied here.
std::optional<int> base_weapon_range_from_cells(double cells, int fallback);
// Resolved original inputs for Unit virtual +350 and 006C4BB0.
struct CannonRangeInput {
    sim::Coord origin, target; // Original resolved virtual +48 positions.
    int weapon_range, minimum_range; // WeaponType +B4 and +B8.
    double gravity; // Rules +1320.
    // Prove that projectile +26B is set, +267 and +265 are clear, the
    // target is ground-level, and its Cell +140 bit 100 is clear.
    std::optional<bool> plain_cannon_and_ground;
    // Original 004BBAA0 result. False is clear. It is needed only after
    // distance and ballistic checks pass.
    std::optional<bool> line_blocked;
};
enum class CannonRangeStatus { InRange, OutOfRange, NeedsInput };
// Base 006C4BB0 Cannon branch. This does not run the line trace or check
// turret facing. The caller supplies the actual range and virtual positions.
CannonRangeStatus base_cannon_range(const CannonRangeInput&, const sim::OriginalDistance&);
}
