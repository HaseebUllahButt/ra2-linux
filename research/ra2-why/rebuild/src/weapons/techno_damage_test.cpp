#include "techno_damage.h"
#include <iostream>
using namespace ra2::weapons;
int main() {
  TechnoDamageInput i{};
  while (std::cin >> i.raw_damage >> i.bypass_armor >> i.house_armor_multiplier
         >> i.object_armor_multiplier >> i.veteran >> i.elite >> i.veteran_armor
         >> i.elite_armor >> i.veteran_armor_multiplier >> i.attacker_present
         >> i.suppress_same_type_and_house >> i.same_type >> i.same_house
         >> i.invulnerable_virtual >> i.immune_virtual >> i.type_has_side_effect_5d0
         >> i.warhead_present >> i.warhead_flag_163 >> i.warhead_flag_164
         >> i.type_flag_afe >> i.type_flag_afd) {
    auto r=base_techno_damage_prefix(i);
    std::cout << static_cast<int>(r.stage) << ' ' << r.damage << '\n';
  }
}
