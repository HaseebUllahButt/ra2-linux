#include "detonation.h"
#include <limits>
#include <stdexcept>
namespace ra2::weapons {
std::optional<sim::Coord> base_unit_cannon_impact_position(const UnitCannonImpactInput& in,
    const sim::OriginalDistance& distance) {
    if (!in.target_present || !in.ordinary_ground_target || !*in.ordinary_ground_target) return {};
    if (!*in.target_present) return in.impact;
    if (!in.unit_position) return {};
    const auto safe=[](int a,int b) {const auto d=static_cast<long long>(a)-b;return d>=-1000000 && d<=1000000;};
    if (!safe(in.impact.x,in.unit_position->x) || !safe(in.impact.y,in.unit_position->y) ||
        !safe(in.impact.z,in.unit_position->z)) return {};
    try { return distance.between(in.impact,*in.unit_position)<42?*in.unit_position:in.impact; }
    catch (const std::exception&) { return {}; }
}
bool base_complete_single_cluster_scatter(std::optional<bool> active,sim::Random& rng) {
    if (!active) return false;
    if (*active) { auto trial=rng; (void)trial.ranged(256,512); (void)trial.next(); rng=trial; }
    return true;
}
}
