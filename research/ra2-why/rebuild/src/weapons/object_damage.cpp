#include "object_damage.h"
#include <sim/fp.h>
#include <cmath>
#include <stdexcept>

namespace ra2::weapons {
ObjectDamagePlan base_object_damage_prefix(const ObjectDamageInput &in) {
  constexpr int bound = 10000000;
  if (in.maximum_health < 1 || in.maximum_health > bound || in.health < 0 ||
      in.health > in.maximum_health || in.raw_damage < -bound ||
      in.raw_damage > bound || !std::isfinite(in.condition_red) ||
      in.condition_red < 0 || in.condition_red > 1)
    throw std::invalid_argument("object damage outside verified domain");

  ObjectDamagePlan out{ObjectDamageStage::NoChange, in.health, in.raw_damage, 0};
  if (in.health == 0 || in.raw_damage == 0 || (!in.bypass_armor && in.immune))
    return out;
  if (!in.bypass_armor) {
    if (!in.resolved_damage) {
      out.stage = ObjectDamageStage::NeedsArmor;
      return out;
    }
    if (*in.resolved_damage < -bound || *in.resolved_damage > bound)
      throw std::invalid_argument("resolved damage outside verified domain");
    out.damage = *in.resolved_damage;
  }
  if (!out.damage)
    return out;
  if (out.damage < 0) {
    const auto healed = static_cast<long long>(in.health) - out.damage;
    out.health = static_cast<int>(healed > in.maximum_health ? in.maximum_health : healed);
    if (out.health != in.health)
      out.stage = ObjectDamageStage::HealingCallback;
    return out;
  }

  if (out.damage >= in.health)
    out.damage = in.health;
  out.health = in.health - out.damage;
  out.severity = 1;
  const int half = in.maximum_health >> 1;
  if (in.health >= half && out.health < half && out.health > 0)
    out.severity = 2;
  const double red = sim::fp::multiply(in.maximum_health, in.condition_red);
  if (red < in.health && out.health < red)
    out.severity = 3;
  out.stage = ObjectDamageStage::DamageCallbacks;
  return out;
}

UnitDeathPlan base_unit_death_branch(const UnitDeathBranchInput &input) {
  if (input.type_field_bb8 <= 0)
    return {UnitDeathBranch::OrdinaryDeath, input.unit_field_5c0, input.health,
            input.active};
  if (input.unit_field_5c0 == -1)
    return {UnitDeathBranch::NeedsVirtual360, 0, input.health, input.active};
  return {UnitDeathBranch::RestoredNeedsContinuation, input.unit_field_5c0, 1,
          true};
}
} // namespace ra2::weapons
