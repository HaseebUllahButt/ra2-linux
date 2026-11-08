#include "weapons.h"
#include <iostream>
#include <stdexcept>
using namespace ra2::weapons;
int main() {
  try {
    StandardAreaImpactInput input{};
    input.position={11,-7,333}; input.attacker_id=0x1002000; input.warhead_id=0x1003000;
    if (base_standard_area_impact(input)) throw std::runtime_error("unknown guards accepted");
    input.special_warhead=false; input.pre_area_effects=false;
    while (std::cin >> input.projectile_damage >> input.damage_multiplier) {
      const auto event=base_standard_area_impact(input);
      if (!event) throw std::runtime_error("standard area event missing");
      std::cout << event->position.x << ' ' << event->position.y << ' ' << event->position.z
                << ' ' << event->attacker_id << ' ' << event->warhead_id << ' '
                << event->damage << '\n';
    }
    input.special_warhead=true;
    if (base_standard_area_impact(input)) throw std::runtime_error("special warhead accepted");
    input.special_warhead=false; input.pre_area_effects=true;
    if (base_standard_area_impact(input)) throw std::runtime_error("unresolved pre-effects accepted");
    return 0;
  } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
