#pragma once

#include <sim/combat.h>
#include <sim/distance.h>
#include <sim/target.h>
#include <sim/world.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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

// First sampled cell in the base terrain trace, 0x4BBB50..0x4BBC19.
// A positive result needs further distance and adjacent-cell checks.
struct TerrainTraceFirstCell {
  bool checks_cliffs, checks_walls;
  // 0x47CF10: signed cell level plus four for Cell +0x140 bit 0x80.
  int sample_floor, reference_floor, source_floor;
  bool sample_is_destination, sample_has_wall;
  int source_level, destination_level; // Signed cell height bytes.
  bool allied_wall_rule, sample_has_owner, owner_allied;
};
bool base_trace_first_cell_candidate(const TerrainTraceFirstCell &);

// Base 0x4BBAF0 with a stable map. The lookup must return the same cell and
// state for a given coordinate for the duration of this call. An absent
// optional means the caller cannot resolve the cell, including the original
// out-of-map fallback cell. Cell IDs must be nonzero and unique.
struct TerrainTraceCell {
  std::uint32_t id;
  int floor; // 0x47CF10: signed level plus four for Cell +0x140 bit 0x80.
  bool wall, has_owner, owner_allied;
};
struct TerrainTraceInput {
  std::array<std::int32_t, 3> position;
  std::uint32_t destination_cell_id;
  int reference_floor, source_floor, source_level, destination_level;
  bool checks_cliffs, checks_walls, allied_wall_rule;
};
enum class TerrainTraceStatus { Clear, Hit, NeedsCell };
struct TerrainTraceResult {
  TerrainTraceStatus status;
  std::uint32_t cell_id; // Nonzero only for Hit.
};
using TerrainTraceLookup =
    std::function<std::optional<TerrainTraceCell>(std::int16_t, std::int16_t)>;
TerrainTraceResult base_trace_terrain(const TerrainTraceInput &,
                                      const TerrainTraceLookup &);

// Base 0x471660 and 0x4608B1..0x4609DF. Objects are the resolved ground
// cell list in original linked-list order. Selection uses the low bytes of
// the virtual position. Collision uses the separate stored position.
struct ProjectileCellObject {
  std::uint32_t id;
  sim::Coord virtual_position, stored_position;
  bool eligible_flag, allied_to_source;
};
struct ProjectileObjectCollision {
  std::uint32_t selected_id; // Zero means no eligible object.
  bool collision;
  sim::Coord position; // Snaps to the object unless preserve_position is set.
};
ProjectileObjectCollision base_projectile_object_collision(
    sim::Coord position, std::uint32_t source_id, bool preserve_position,
    const std::vector<ProjectileCellObject> &, const sim::OriginalDistance &);

// A guarded, flat-ground Cannon tick. The caller resolves map and object
// snapshots at the proposed position. Empty optionals mean unknown state.
struct FlatCannonTickInput {
  ArcInput arc;
  sim::Coord target;
  int height_before, height_after, height_unit, proposed_floor;
  bool old_cell_bridge_100, proposed_cell_bridge_100;
  std::uint32_t source_id;
  bool preserve_object_position;
  std::optional<std::uint32_t> proposed_building_id, target_building_id;
  std::optional<bool> position_in_map;
  // Whether the original building-interception path absorbs the projectile
  // after movement. Resolve at the final position, including object snaps.
  std::optional<bool> building_intercepts;
  std::optional<std::vector<ProjectileCellObject>> cell_objects;
  TerrainTraceInput terrain;
};
enum class FlatCannonTickStatus { Continue, Impact, Remove, NeedsInput };
struct FlatCannonTickResult {
  FlatCannonTickStatus status;
  ArcProposal state;
  std::uint32_t object_id;
};
// Near-ground motion without a source, bridges, interception, and slow initial
// velocity remain unresolved. Impact does not dispatch damage.
FlatCannonTickResult base_flat_cannon_tick(const FlatCannonTickInput &,
                                           const TerrainTraceLookup &,
                                           const sim::OriginalDistance &);

// Bind a no-overlay, resolved flat source and proposed cell from Simulation.
// Leave the input unchanged on false. This only resolves floor and bridge;
// object, building, trace and original height inputs remain the caller's job.
bool bind_flat_tick_cells(FlatCannonTickInput &, const sim::TerrainCell &old_cell,
                          const sim::TerrainCell &proposed_cell);

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
