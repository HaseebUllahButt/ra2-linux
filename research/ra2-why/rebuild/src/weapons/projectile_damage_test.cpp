#include "projectile_damage.h"
#include <iostream>
using namespace ra2::weapons;
int main() {
 ProjectileDamageInput in{};
 while (std::cin >> in.weapon_damage >> in.sonic >> in.fire_particles >> in.house_firepower >> in.object_firepower
        >> in.veteran >> in.elite >> in.veteran_firepower >> in.elite_firepower >> in.veteran_multiplier) {
  auto n=base_projectile_damage(in);if (!n) return 1;std::cout<<*n<<'\n';
 }
}
