#pragma once
#include <cstdint>

namespace ra2::weapons {
// Base Techno 006CDC40 through the call to Object damage. All fields are
// original, resolved state. This does not perform the Object/Unit handlers.
struct TechnoDamageInput {
  int raw_damage;
  bool bypass_armor;
  double house_armor_multiplier, object_armor_multiplier;
  bool veteran, elite, veteran_armor, elite_armor;
  double veteran_armor_multiplier;
  bool attacker_present, suppress_same_type_and_house, same_type, same_house;
  bool invulnerable_virtual, immune_virtual;
  bool type_has_side_effect_5d0;
  bool warhead_present, warhead_flag_163, warhead_flag_164;
  bool type_flag_afe, type_flag_afd;
};
enum class TechnoDamageStage {
  ObjectDamage, SameTypeImmunity, InvulnerabilityEffect, Immune,
  NeedsTypeSideEffect, WarheadImmunity
};
struct TechnoDamageResult {
  TechnoDamageStage stage;
  int damage;
};
// Rejects exceptional numeric inputs. InvulnerabilityEffect needs the real
// 0047F600 callback before the caller writes the zero damage value.
TechnoDamageResult base_techno_damage_prefix(const TechnoDamageInput &);
}
