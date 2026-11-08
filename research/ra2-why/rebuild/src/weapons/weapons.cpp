#include "weapons.h"
#include <sim/fp.h>
#include <algorithm>
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

bool base_lobber_branch(const LobberInputs &input,
                        const sim::OriginalDistance &lookup) {
  if (!input.weapon_present || input.lobber) return true;
  if (!input.current_target) return false;
  const auto &target = *input.current_target;
  const auto dx = std::int64_t(input.shooter.x) - target.x;
  const auto dy = std::int64_t(input.shooter.y) - target.y;
  const auto dz = std::int64_t(target.z) - input.shooter.z;
  if (dx < -100000 || dx > 100000 || dy < -100000 || dy > 100000 ||
      dz < std::numeric_limits<int>::min() || dz > std::numeric_limits<int>::max())
    throw std::range_error("lobber target outside verified range");
  return dz > 0 && lookup.between(input.shooter, target, true) < dz;
}

int base_ballistic_launch_speed(int distance, double gravity,
                                const sim::OriginalDistance &lookup) {
  if (distance < 0 || !std::isfinite(gravity) || gravity < 0)
    throw std::invalid_argument("invalid ballistic speed inputs");
  const auto scaled = sim::fp::multiply(sim::fp::multiply(distance, gravity), 1.2);
  return truncate_int(lookup.square_root(scaled));
}

int base_limit_initial_speed(int speed, const std::array<std::int32_t, 3> &d,
                             const sim::OriginalDistance &lookup) {
  if (speed < 0) throw std::invalid_argument("negative initial speed");
  for (auto value : d)
    if (value < -100000 || value > 100000)
      throw std::range_error("initial displacement outside verified range");
  const auto xx = sim::fp::multiply(d[0], d[0]);
  const auto yy = sim::fp::multiply(d[1], d[1]);
  const auto zz = sim::fp::multiply(d[2], d[2]);
  const int distance = truncate_int(lookup.square_root(sim::fp::add(sim::fp::add(yy, zz), xx)));
  return std::min(speed, distance / 2);
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
  auto read_table = [&](auto &table, std::uint32_t address) {
    file.seekg(address - 0x400000);
    for (auto &value : table) {
      unsigned char b[4];
      file.read(reinterpret_cast<char *>(b), sizeof b);
      if (!file) throw std::runtime_error("truncated base angle table");
      const std::uint32_t bits = std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8 |
                                 std::uint32_t(b[2]) << 16 | std::uint32_t(b[3]) << 24;
      std::memcpy(&value, &bits, sizeof value);
      if (!std::isfinite(value)) throw std::runtime_error("invalid base angle table");
    }
  };
  read_table(out.angles_, 0x80bdd4);
  read_table(out.atan_, 0x813df4);
  read_table(out.sine_, 0x801dc4);
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

float BaseBallisticAngles::trig(double angle, bool cosine) const {
  constexpr double table_scale = 2607.594482421875;
  const int units = truncate_int(sim::fp::multiply(angle, table_scale));
  int index = (units / 2) % 8192;
  if (index < 0) index += 8192;
  if (cosine) index += 2048;
  if ((units & 1) && index < (cosine ? 10239 : 8191)) ++index;
  return sine_[std::size_t(index)];
}

double BaseBallisticAngles::atan_vector(double y, double x) const {
  constexpr double atan_step = 0.024413354694843292;
  constexpr float half_pi_float = 1.5707963705062866f;
  constexpr double pi = 3.141592653589793;
  auto single_toward_zero = [](double value) {
    float result = static_cast<float>(value);
    if (std::abs(double(result)) > std::abs(value))
      result = std::nextafter(result, 0.0f);
    return result;
  };
  const float fy = single_toward_zero(y), fx = single_toward_zero(x);
  if (fx == 0) {
    if (fy == 0) return 0;
    return fy > 0 ? half_pi_float : -half_pi_float;
  }
  const auto ratio = sim::fp::divide(sim::fp::divide(fy, fx), atan_step);
  if (std::abs(ratio) > std::numeric_limits<int>::max())
    throw std::range_error("angle ratio outside verified range");
  const int index = std::abs(truncate_int(ratio));
  double angle = index < 4097 ? atan_[std::size_t(index)] : half_pi_float;
  if (fx < 0) angle = sim::fp::add(pi, -angle);
  if (fy < 0) angle = -angle;
  return angle;
}

std::uint16_t BaseBallisticAngles::heading(std::int32_t dx, std::int32_t dy) const {
  if (dy == std::numeric_limits<std::int32_t>::min())
    throw std::range_error("heading displacement outside verified range");
  constexpr double half_pi = 1.5707963267948966;
  constexpr double direction_scale = -10430.060040584269;
  return static_cast<std::uint16_t>(truncate_int(sim::fp::multiply(
      sim::fp::add(atan_vector(-double(dy), dx), -half_pi), direction_scale)));
}

std::array<double, 3>
BaseBallisticAngles::horizontal_velocity(int speed, std::uint16_t heading) const {
  if (speed < 0) throw std::invalid_argument("negative initial speed");
  constexpr double direction_unit = -9.587672516830327e-05;
  const double initial = sim::fp::multiply(
      sim::fp::divide(speed, distance_.square_root(10000)), 100);
  const double x = initial == 0 ? 100 : initial;
  const double magnitude = distance_.square_root(sim::fp::multiply(x, x));
  const int signed_heading = heading < 32768 ? heading : int(heading) - 65536;
  const double angle = sim::fp::multiply(signed_heading - 0x3fff, direction_unit);
  return {sim::fp::multiply(trig(angle, true), magnitude),
          -sim::fp::multiply(trig(angle, false), magnitude), 0};
}

std::optional<BaseTankLaunch>
BaseBallisticAngles::tank_launch(int speed, const std::array<std::int32_t, 3> &d,
                                 double gravity, bool alternate) const {
  if (!std::isfinite(gravity) || gravity < 0)
    throw std::invalid_argument("invalid projectile gravity");
  const int limited = base_limit_initial_speed(speed, d, distance_);
  if (!limited) return std::nullopt;
  const int horizontal = truncate_int(distance_.square_root(sim::fp::add(
      sim::fp::multiply(d[0], d[0]), sim::fp::multiply(d[1], d[1]))));
  if (!horizontal) return std::nullopt;
  const auto direction = heading(d[0], d[1]);
  auto elevation = angle(limited, horizontal, d[2], gravity, alternate);
  if (!elevation) return std::nullopt;
  auto v = pitch_velocity(horizontal_velocity(limited, direction), *elevation);
  return BaseTankLaunch{limited, direction, *elevation, v};
}

std::array<double, 3>
BaseBallisticAngles::pitch_velocity(std::array<double, 3> v,
                                    std::uint16_t pitch) const {
  using namespace sim::fp;
  constexpr double half_pi = 1.5707963267948966;
  constexpr double direction_scale = -10430.060040584269;
  constexpr double direction_unit = -9.587672516830327e-05;
  const double xy = add(multiply(v[1], v[1]), multiply(v[0], v[0]));
  const float horizontal = distance_.square_root(xy);
  const double elevation = atan_vector(v[2], horizontal);
  const auto encoded = static_cast<std::uint16_t>(truncate_int(
      multiply(add(elevation, -half_pi), direction_scale)));
  const int signed_encoded = encoded < 32768 ? encoded : int(encoded) - 65536;
  const double old_pitch = multiply(signed_encoded - 0x3fff, direction_unit);
  const double magnitude = distance_.square_root(
      add(add(multiply(v[0], v[0]), multiply(v[1], v[1])), multiply(v[2], v[2])));
  if (old_pitch != 0) {
    const double cosine = trig(old_pitch, true);
    if (cosine == 0) throw std::range_error("vertical velocity outside verified domain");
    v[0] = divide(v[0], cosine);
    v[1] = divide(v[1], cosine);
  }
  const int signed_pitch = pitch < 32768 ? pitch : int(pitch) - 65536;
  const double new_pitch = multiply(signed_pitch - 0x3fff, direction_unit);
  const double cosine = trig(new_pitch, true);
  v[0] = multiply(v[0], cosine);
  v[1] = multiply(v[1], cosine);
  v[2] = multiply(trig(new_pitch, false), magnitude);
  return v;
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

bool base_trace_first_cell_candidate(const TerrainTraceFirstCell &input) {
  if (input.sample_floor < -128 || input.sample_floor > 131 ||
      input.reference_floor < -128 || input.reference_floor > 131 ||
      input.source_floor < -128 || input.source_floor > 131 ||
      input.source_level < -128 || input.source_level > 127 ||
      input.destination_level < -128 || input.destination_level > 127)
    throw std::invalid_argument("invalid terrain cell level");
  if (input.checks_cliffs &&
      static_cast<std::int64_t>(input.sample_floor) - input.reference_floor >= 4 &&
      input.sample_floor > input.source_floor)
    return true;
  if (!input.checks_walls || input.sample_is_destination ||
      !input.sample_has_wall || input.source_level > input.destination_level)
    return false;
  return !(input.allied_wall_rule && input.sample_has_owner && input.owner_allied);
}

TerrainTraceResult base_trace_terrain(const TerrainTraceInput &input,
                                      const TerrainTraceLookup &lookup) {
  if (!input.destination_cell_id || !lookup)
    throw std::invalid_argument("invalid terrain trace input");
  // The game divides towards zero, then stores the low 16 bits.
  const auto cell_coordinate = [](std::int32_t world) {
    const auto bits = static_cast<std::uint32_t>(world / 256) & 0xffffu;
    return static_cast<std::int16_t>(bits < 0x8000 ? static_cast<int>(bits)
                                                   : static_cast<int>(bits) - 0x10000);
  };
  const auto cell = lookup(cell_coordinate(input.position[0]),
                           cell_coordinate(input.position[1]));
  if (!cell)
    return {TerrainTraceStatus::NeedsCell, 0};
  if (!cell->id)
    throw std::invalid_argument("terrain trace cell ID zero");
  const TerrainTraceFirstCell candidate{
      input.checks_cliffs, input.checks_walls, cell->floor,
      input.reference_floor, input.source_floor,
      cell->id == input.destination_cell_id, cell->wall,
      input.source_level, input.destination_level, input.allied_wall_rule,
      cell->has_owner, cell->owner_allied};
  if (!base_trace_first_cell_candidate(candidate))
    return {TerrainTraceStatus::Clear, 0};
  // The remaining geometry branch repeats 0x548070 for the same packed cell
  // and repeats the same candidate tests. A stable map returns this same cell.
  return {TerrainTraceStatus::Hit, cell->id};
}

bool base_area_impact_enabled(int raw_damage, bool scenario_blocks_damage,
                              bool warhead_present) {
  return raw_damage != 0 && !scenario_blocks_damage && warhead_present;
}

std::optional<DamageRequest>
base_resolved_impact_recipient(const ResolvedImpactRecipient &input) {
  if (!input.target_id || input.distance < 0 || input.radius < 0)
    throw std::invalid_argument("invalid impact recipient");
  if (!base_area_impact_enabled(input.raw_damage, input.scenario_blocks_damage,
                                input.warhead_id != 0))
    return std::nullopt;
  if (!input.active || (input.original_type == 6 && input.type6_immune) ||
      (input.special_group && (!input.special_flag || !input.special_virtual)))
    return std::nullopt;
  const int distance = input.original_type == 2 && input.type2_virtual_54
                           ? input.distance / 2
                           : input.distance;
  if (input.health <= 0 || !input.receives_damage || input.in_limbo ||
      distance > input.radius)
    return std::nullopt;
  return DamageRequest{input.target_id, input.attacker_id, input.warhead_id,
                       input.raw_damage, distance};
}

DamageEvent resolved_damage_event(std::uint32_t target_id,
                                  const sim::DamageInputs &input) {
  if (!target_id)
    throw std::invalid_argument("target ID zero is reserved");
  return {target_id, sim::total_damage(input)};
}
} // namespace ra2::weapons
