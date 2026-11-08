#include "weapons.h"
#include <sim/fp.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ra2::weapons {
namespace {
int truncate_int(double value) {
  if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    throw std::range_error("projectile coordinate outside verified range");
  return static_cast<int>(std::trunc(value));
}
} // namespace

sim::CandidateDecision assess_target_for_scan(const sim::CandidatePrefix &input) {
  return sim::check_candidate_prefix(input);
}

ProjectileSpawn base_resolved_tank_fire(const ResolvedTankFire &input,
                                        const sim::OriginalDistance &lookup) {
  if (!input.source_id || !input.target_id || !input.projectile_type_id ||
      !input.warhead_type_id)
    throw std::invalid_argument("tank fire requires resolved IDs");
  const auto dx = std::int64_t(input.target[0]) - input.muzzle[0];
  const auto dy = std::int64_t(input.target[1]) - input.muzzle[1];
  if (dx < -46340 || dx > 46340 || dy < -46340 || dy > 46340 ||
      dx * dx + dy * dy > std::numeric_limits<int>::max())
    throw std::range_error("tank firing distance outside verified range");
  const int distance = truncate_int(lookup.square_root(double(dx * dx + dy * dy)));
  return {input.source_id, input.target_id, input.projectile_type_id,
          input.warhead_type_id, input.muzzle, input.target, input.damage, distance,
          base_ballistic_launch_speed(distance, input.gravity, lookup),
          input.projectile_flag};
}

int base_ballistic_launch_speed(int distance, double gravity,
                                const sim::OriginalDistance &lookup) {
  if (distance < 0 || !std::isfinite(gravity) || gravity < 0)
    throw std::invalid_argument("invalid ballistic speed inputs");
  const auto scaled = sim::fp::multiply(sim::fp::multiply(distance, gravity), 1.2);
  return truncate_int(lookup.square_root(scaled));
}

ArcProposal base_arc_proposal(const ArcInput &input) {
  if (!std::isfinite(input.gravity) || input.gravity < 0)
    throw std::invalid_argument("invalid projectile gravity");
  ArcProposal result{input.position, input.velocity};
  result.velocity[2] = sim::fp::add(input.velocity[2], -input.gravity);
  for (std::size_t axis = 0; axis < 3; ++axis)
    result.position[axis] = truncate_int(sim::fp::add(input.position[axis],
                                                       result.velocity[axis]));
  return result;
}

ImpactStatus base_cannon_impact(const CannonImpactInputs &input) {
  if (input.original_height_unit < 0 ||
      input.original_height_unit > std::numeric_limits<int>::max() / 4)
    throw std::invalid_argument("invalid original height unit");
  if (!input.terrain_trace_hit)
    return ImpactStatus::NeedsTrace;
  if (*input.terrain_trace_hit ||
      input.height_above_floor <= -4 * input.original_height_unit)
    return ImpactStatus::Impact;
  return ImpactStatus::Continue;
}

DamageEvent resolved_damage_event(std::uint32_t target_id,
                                  const sim::DamageInputs &input) {
  if (!target_id)
    throw std::invalid_argument("target ID zero is reserved");
  return {target_id, sim::total_damage(input)};
}
} // namespace ra2::weapons
