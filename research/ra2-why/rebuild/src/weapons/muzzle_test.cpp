#include "muzzle.h"
#include <iostream>
using namespace ra2::weapons;
int main() {
 FlatUnitMuzzleInput x{};unsigned hull,turret;
 while (std::cin >> x.position.x >> x.position.y >> x.position.z >> hull >> turret
        >> x.fire_flh[0] >> x.fire_flh[1] >> x.fire_flh[2] >> x.turret_offset >> x.burst_index) {
  x.hull_facing=static_cast<std::uint16_t>(hull);x.turret_facing=static_cast<std::uint16_t>(turret);x.flat_drive_locomotor=true;
  const auto p=base_flat_unit_muzzle(x);if (!p) return 1;
  std::cout << p->x << ' ' << p->y << ' ' << p->z << '\n';
 }
}
