#include "weapons.h"
#include <sim/fp.h>
#include <cmath>
#include <cstring>
#include <fstream>
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

BaseBallisticAngles BaseBallisticAngles::from_executable(const std::string &path) {
  BaseBallisticAngles out;
  out.distance_ = sim::OriginalDistance::from_executable(path);
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open base executable");
  constexpr unsigned char signature[] = {0xdd, 0x44, 0x24, 0x04, 0xd8, 0x05,
                                         0xf8, 0xb7, 0x79, 0x00};
  unsigned char actual[sizeof signature]{};
  file.seekg(0x4ba580 - 0x400000);
  file.read(reinterpret_cast<char *>(actual), sizeof actual);
  if (!file || std::memcmp(actual, signature, sizeof signature))
    throw std::runtime_error("unsupported base angle table");
  file.seekg(0x80bdd4 - 0x400000);
  for (auto &value : out.angles_) {
    unsigned char b[4];
    file.read(reinterpret_cast<char *>(b), sizeof b);
    if (!file) throw std::runtime_error("truncated base angle table");
    const std::uint32_t bits = std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
                               std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
    std::memcpy(&value, &bits, sizeof value);
    if (!std::isfinite(value)) throw std::runtime_error("invalid base angle table");
  }
  return out;
}

std::optional<std::uint16_t>
BaseBallisticAngles::angle(int speed, int distance, int height, double gravity,
                            bool alternate) const {
  if (speed <= 0 || distance <= 0 || distance == std::numeric_limits<int>::max() ||
      !std::isfinite(gravity) || gravity < 0)
    throw std::invalid_argument("invalid ballistic angle inputs");
  constexpr double half_pi = 1.5707963267948966;
  constexpr double direction_scale = -10430.060040584269;
  auto solve = [&](int horizontal, bool branch) -> std::optional<double> {
    if (height > 0) return 0x1.921d9f4d37c12p-1;
    using namespace sim::fp;
    const double v2 = multiply(speed, speed);
    const double h2 = multiply(height, height);
    const double d2 = multiply(horizontal, horizontal);
    const double term = multiply(multiply(v2, height), gravity);
    const double discriminant = add(add(multiply(v2, v2), -add(term, term)),
                                    -multiply(multiply(gravity, gravity), d2));
    const double denominator = multiply(add(divide(h2, d2), 1), 2);
    if (discriminant < 0 || denominator == 0) return std::nullopt;
    const double root = distance_.square_root(discriminant);
    const double numerator = add(add(v2, -multiply(height, gravity)),
                                 branch ? -root : root);
    const double v_square = divide(numerator, denominator);
    if (v_square < 0) return std::nullopt;
    const double ratio = divide(distance_.square_root(v_square), speed);
    const int table_index = -truncate_int(multiply(add(ratio, 1), -2048));
    if (table_index < 0 || table_index >= int(angles_.size()))
      throw std::range_error("ballistic angle table index outside verified range");
    return add(half_pi, -double(angles_[std::size_t(table_index)]));
  };
  auto first = solve(distance, alternate);
  if (!first) return std::nullopt;
  if (!alternate) {
    auto second = solve(distance + 1, false);
    if (!second) return std::nullopt;
    if (*second < *first) *first = -*first;
  }
  const double direction = sim::fp::multiply(sim::fp::add(*first, -half_pi),
                                              direction_scale);
  return static_cast<std::uint16_t>(truncate_int(direction));
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
