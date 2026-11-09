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

ObjectDamagePathResult base_object_damage_path(
    const ObjectDamageInput &input, ObjectDamageState state, bool attacker,
    const ObjectDamageCallback &callback) {
  if (!callback || state.health != input.health ||
      state.maximum_health != input.maximum_health || !state.type_present)
    throw std::invalid_argument("object damage snapshot is incomplete");
  const auto plan = base_object_damage_prefix(input);
  state.health = plan.health;
  int status = plan.severity;
  const auto result = [&](int value, bool needs_armor = false) {
    return ObjectDamagePathResult{state, plan.damage, value, needs_armor};
  };
  const auto emit = [&](ObjectDamageActionKind kind, int argument,
                        bool with_attacker = false) {
    callback(ObjectDamageAction{kind, argument, with_attacker}, state);
  };
  if (plan.stage == ObjectDamageStage::NeedsArmor)
    return result(0, true);
  if (plan.stage == ObjectDamageStage::NoChange)
    return result(0);
  if (plan.stage == ObjectDamageStage::HealingCallback) {
    emit(ObjectDamageActionKind::HealingVirtual, 7);
    return result(0);
  }

  if (state.health == 0 && !input.bypass_armor &&
      (!state.original_kind || *state.original_kind == 15))
    throw std::invalid_argument("original infantry death branch is unresolved");

  const int written_health = state.health;
  if (status == 2) {
    if (attacker && state.has_trigger)
      emit(ObjectDamageActionKind::Trigger, 0x27);
    if (!state.active)
      return result(5);
    if (state.has_trigger)
      emit(ObjectDamageActionKind::Trigger, 0x2a);
  }
  if (!state.active)
    return result(5);
  if (status == 3) {
    if (attacker && state.has_trigger)
      emit(ObjectDamageActionKind::Trigger, 0x28);
    if (!state.active)
      return result(5);
    if (state.has_trigger)
      emit(ObjectDamageActionKind::Trigger, 0x2b);
  }
  if (!state.active)
    return result(5);
  if (state.health != input.health && state.type_present &&
      state.maximum_health == input.health) {
    if (attacker && state.has_trigger)
      emit(ObjectDamageActionKind::Trigger, 0x26);
    if (state.has_trigger && state.active)
      emit(ObjectDamageActionKind::Trigger, 0x29);
    if (state.has_trigger) {
      if (!state.active)
        return result(5);
      if (attacker)
        emit(ObjectDamageActionKind::Trigger, 0x29, true);
    }
  }
  if (!state.active || (written_health > 0 && state.health <= 0))
    return result(5);
  if (state.health == 0) {
    emit(ObjectDamageActionKind::DeathVirtual, 0, attacker);
    status = 4;
    emit(ObjectDamageActionKind::DestroyVirtual, 1);
  }
  if (!state.active)
    return result(status);
  if (attacker && state.has_trigger && status != 4)
    emit(ObjectDamageActionKind::Trigger, 6, true);
  if (!state.active)
    return result(status);
  if (attacker && state.has_trigger && status != 4)
    emit(ObjectDamageActionKind::Trigger, 0x2c, true);
  if (state.active && status != 0 && state.redraw)
    emit(ObjectDamageActionKind::RedrawVirtual, 2);
  return result(status);
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
