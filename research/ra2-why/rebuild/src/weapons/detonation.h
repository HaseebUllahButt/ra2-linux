#pragma once
#include <sim/distance.h>
#include <sim/sim.h>
#include <optional>
namespace ra2::weapons {
struct UnitCannonImpactInput {
    sim::Coord impact;
    std::optional<bool> target_present;
    std::optional<sim::Coord> unit_position;
    // Prove the target is a Unit whose virtual +54 is false, projectile
    // +272 and +264 are false, +26B is true, and warhead +152 is false.
    std::optional<bool> ordinary_ground_target;
};
// Base 00461DE0 position adjustment for an ordinary ground Unit target.
// Moving Units are allowed: their original +58 reads the stored position.
// Missing target or type data returns empty rather than a guessed position.
std::optional<sim::Coord> base_unit_cannon_impact_position(
    const UnitCannonImpactInput&,const sim::OriginalDistance&);
// Base 00462098..004620E4 for a proven one-cluster, non-airburst
// projectile. Call only after the real area callback has finished. The last
// scatter position is not used, but its two RNG operations still occur.
// A missing active flag leaves RNG unchanged and returns false.
bool base_complete_single_cluster_scatter(std::optional<bool> projectile_active,sim::Random&);
}
