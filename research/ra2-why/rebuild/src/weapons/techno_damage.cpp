#include "techno_damage.h"
#include <sim/fp.h>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ra2::weapons {
namespace {
int original_integer(double value) {
  if (!std::isfinite(value) || value < std::numeric_limits<int>::min() ||
      value > std::numeric_limits<int>::max())
    throw std::range_error("Techno damage conversion outside verified domain");
  return static_cast<int>(value);
}
}
TechnoDamageResult base_techno_damage_prefix(const TechnoDamageInput &in) {
  int damage = in.raw_damage;
  const bool negative = damage < 0;
  if (!in.bypass_armor && !negative) {
    if (!(in.house_armor_multiplier > 0) || !(in.object_armor_multiplier > 0))
      throw std::invalid_argument("Techno armor multipliers must be positive");
    const double divisor = sim::fp::multiply(in.house_armor_multiplier,
                                             in.object_armor_multiplier);
    damage = original_integer(sim::fp::divide(damage, divisor));
    if ((in.veteran && in.veteran_armor) ||
        (in.elite && (in.veteran_armor || in.elite_armor))) {
      if (!(in.veteran_armor_multiplier > 0))
        throw std::invalid_argument("veteran armor multiplier must be positive");
      damage = original_integer(sim::fp::divide(damage, in.veteran_armor_multiplier));
    }
    if (damage < 1) damage = 1;
    if (in.attacker_present && in.suppress_same_type_and_house &&
        in.same_type && in.same_house)
      return {TechnoDamageStage::SameTypeImmunity, damage};
  }
  if (in.invulnerable_virtual && !in.bypass_armor && !negative)
    return {TechnoDamageStage::InvulnerabilityEffect, damage};
  if (in.immune_virtual) return {TechnoDamageStage::Immune, 0};
  if (in.type_has_side_effect_5d0)
    return {TechnoDamageStage::NeedsTypeSideEffect, damage};
  if (in.warhead_present && ((in.warhead_flag_163 && in.type_flag_afe) ||
                             (in.warhead_flag_164 && in.type_flag_afd)))
    return {TechnoDamageStage::WarheadImmunity, 0};
  return {TechnoDamageStage::ObjectDamage, damage};
}
}
