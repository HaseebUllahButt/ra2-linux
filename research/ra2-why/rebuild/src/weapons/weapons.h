#pragma once

#include <sim/combat.h>
#include <sim/distance.h>
#include <sim/target.h>
#include <array>
#include <cstdint>
#include <string>

namespace ra2::weapons {
// This is a target-scan prefix, not a firing decision. Continue does not
// prove that the unit may fire.
sim::CandidateDecision assess_target_for_scan(const sim::CandidatePrefix &);

// 0x733F70 selects 0x47FB70 for ROT=0 projectiles. The distance is the
// caller's already resolved horizontal distance, in original world units.
// Uses the original executable's square-root table.
int base_ballistic_launch_speed(int distance, double gravity,
                                const sim::OriginalDistance &);

// Base 0x47F8B0 / 0x47F9B0, including the executable's angle table.
// A missing result means the original reports no usable solution, or the
// second-solution value needed by its caller cannot be established.
class BaseBallisticAngles {
public:
  static BaseBallisticAngles from_executable(const std::string &path);
  std::optional<std::uint16_t> angle(int speed, int horizontal_distance,
                                      int height_delta, double gravity,
                                      bool alternate) const;
private:
  sim::OriginalDistance distance_;
  std::array<float, 4097> angles_{};
};

// Use only after the original firing checks and weapon selection have been
// resolved. This event does not mean that firing eligibility was checked.
struct ResolvedTankFire {
  std::uint32_t source_id, target_id, projectile_type_id, warhead_type_id;
  std::array<std::int32_t, 3> muzzle, target;
  int damage;
  double gravity;
  std::uint8_t projectile_flag;
};
struct ProjectileSpawn {
  std::uint32_t source_id, target_id, projectile_type_id, warhead_type_id;
  std::array<std::int32_t, 3> muzzle, target;
  int damage, horizontal_distance, speed;
  std::uint8_t projectile_flag;
};
// Base 0x6CAC65..0x6CACCC, for a projectile with ROT=0.
ProjectileSpawn base_resolved_tank_fire(const ResolvedTankFire &,
                                        const sim::OriginalDistance &);

struct ArcInput {
  std::array<std::int32_t, 3> position;
  std::array<double, 3> velocity;
  double gravity;
};
struct ArcProposal {
  std::array<std::int32_t, 3> position;
  std::array<double, 3> velocity;
};
// Original 0x460397..0x460435 arithmetic before terrain, target and collision
// checks. This is a proposed step, not a complete projectile tick.
ArcProposal base_arc_proposal(const ArcInput &);

enum class ImpactStatus { Continue, Impact, NeedsTrace };
struct CannonImpactInputs {
  // Original 0x4BBAF0 result for the Cannon's cliff and wall checks.
  std::optional<bool> terrain_trace_hit;
  int height_above_floor;
  int original_height_unit; // DAT_00850348
};
// Base 0x461C10, limited to the shipped Cannon flags: cliffs and walls on;
// Level, FlakScatter and AA off. Does not perform the terrain trace.
ImpactStatus base_cannon_impact(const CannonImpactInputs &);

// This event carries only the shared damage arithmetic. Simulation owns health,
// target selection, death effects, shields, and all damage dispatch decisions.
struct DamageEvent {
  std::uint32_t target_id;
  int amount;
};
DamageEvent resolved_damage_event(std::uint32_t target_id,
                                  const sim::DamageInputs &);
} // namespace ra2::weapons
