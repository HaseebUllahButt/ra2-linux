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
// Base 0x6CB0E7..0x6CB15E clamps initial velocity magnitude to half the
// resolved 3-D distance. The displacement is target minus muzzle.
int base_limit_initial_speed(int speed, const std::array<std::int32_t, 3> &,
                             const sim::OriginalDistance &);

// Base 0x47F8B0 / 0x47F9B0, including the executable's angle table.
// A missing result means the original reports no usable solution, or the
// second-solution value needed by its caller cannot be established.
struct BaseTankLaunch {
  int limited_speed;
  std::uint16_t heading, pitch;
  std::array<double, 3> velocity;
};
class BaseBallisticAngles {
public:
  static BaseBallisticAngles from_executable(const std::string &path);
  std::optional<std::uint16_t> angle(int speed, int horizontal_distance,
                                      int height_delta, double gravity,
                                      bool alternate) const;
  // Base 0x6CB605..0x6CB747. Reorient the caller's initial velocity using
  // an already resolved pitch. Does not select yaw or firing eligibility.
  std::array<double, 3> pitch_velocity(std::array<double, 3> velocity,
                                       std::uint16_t pitch) const;
  // Base 0x6CB1AC..0x6CB3F1, normal tank path without burst spread.
  // speed and heading must already be resolved by the caller.
  std::array<double, 3> horizontal_velocity(int speed,
                                             std::uint16_t heading) const;
  // Base 0x6CB062..0x6CB0A2 for ROT=0 and Dropping=false.
  std::uint16_t heading(std::int32_t dx, std::int32_t dy) const;
  // Normal non-burst Cannon path. Supply target minus muzzle and the
  // resolved 0x6D7D20 branch. A missing result means no supported solution.
  std::optional<BaseTankLaunch> tank_launch(int speed,
      const std::array<std::int32_t, 3> &displacement, double gravity,
      bool alternate) const;
private:
  float trig(double angle, bool cosine) const;
  double atan_vector(double y, double x) const;
  sim::OriginalDistance distance_;
  std::array<float, 4097> angles_{};
  std::array<float, 4097> atan_{};
  std::array<float, 10240> sine_{};
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
struct LobberInputs {
  bool weapon_present = false;
  bool lobber = false; // WeaponType [Lobber], +0x10A.
  sim::Coord shooter;
  std::optional<sim::Coord> current_target;
};
// Base 0x6D7D20. This selects an arc branch; it does not approve firing.
bool base_lobber_branch(const LobberInputs &, const sim::OriginalDistance &);
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

// Original area-damage entry checks at 0x47E2E8..0x47E31B.
bool base_area_impact_enabled(int raw_damage, bool scenario_blocks_damage,
                              bool warhead_present);

// A single already-collected recipient of base 0x47E9DC..0x47EAA1.
// Resolve these fields again after each preceding damage event.
struct ResolvedImpactRecipient {
  std::uint32_t target_id, attacker_id, warhead_id;
  int raw_damage, distance, radius, original_type, health;
  bool active, receives_damage, in_limbo, type6_immune;
  bool special_group, special_flag, special_virtual;
  bool type2_virtual_54;
  bool scenario_blocks_damage = false;
};
struct DamageRequest {
  std::uint32_t target_id, attacker_id, warhead_id;
  int raw_damage, distance;
};
// Returns the original damage callback request, or no request. Candidate
// collection and subsequent derived health and death handlers remain open.
std::optional<DamageRequest>
base_resolved_impact_recipient(const ResolvedImpactRecipient &);

// This event carries only the shared damage arithmetic. Simulation owns health,
// target selection, death effects, shields, and all damage dispatch decisions.
struct DamageEvent {
  std::uint32_t target_id;
  int amount;
};
DamageEvent resolved_damage_event(std::uint32_t target_id,
                                  const sim::DamageInputs &);
} // namespace ra2::weapons
