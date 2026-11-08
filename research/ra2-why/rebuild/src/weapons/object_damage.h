#pragma once
#include <optional>
namespace ra2::weapons {
// Base 005D3D20 through the first HP write. This is an isolated plan, not
// Simulation damage. Callers must run the original derived and virtual
// handlers, triggers, death effects and removal before applying results.
struct ObjectDamageInput {
  int health, maximum_health, raw_damage;
  bool bypass_armor, immune;
  double condition_red;
  // Original 0047E1C0 result. Use sim::total_damage for its supported domain.
  std::optional<int> resolved_damage;
};
enum class ObjectDamageStage { NoChange, NeedsArmor, HealingCallback, DamageCallbacks };
struct ObjectDamagePlan {
  ObjectDamageStage stage;
  int health, damage, severity;
};
ObjectDamagePlan base_object_damage_prefix(const ObjectDamageInput &);

// Base Unit damage 006FCA93..006FCAD5 after its parent returns status 4.
// Type +BB8, Unit +5C0 and the virtual +360 call need their original owners.
// Stop before the callback or the remaining death handler.
struct UnitDeathBranchInput {
  int type_field_bb8, unit_field_5c0, health;
  bool active;
};
enum class UnitDeathBranch { OrdinaryDeath, NeedsVirtual360, RestoredNeedsContinuation };
struct UnitDeathPlan {
  UnitDeathBranch branch;
  int unit_field_5c0, health;
  bool active;
};
UnitDeathPlan base_unit_death_branch(const UnitDeathBranchInput &);
}
