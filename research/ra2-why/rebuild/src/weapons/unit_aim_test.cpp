#include <sim/damage_runtime.h>
#include <iostream>
using namespace ra2;
int main() {
 sim::Coord p;
 while (std::cin >> p.x >> p.y >> p.z) {
  session::world::Snapshot w;session::world::Entity a;a.id=1;a.kind=session::world::Kind::Unit;
  a.lifetime=session::world::Lifetime::Placed;auto b=a;b.id=2;b.position=p;w.entities={a,b};
  const auto aim=sim::base_stationary_unit_aim(w,1,2,false);
  if (!aim) return 1;
  std::cout << aim->x << ' ' << aim->y << ' ' << aim->z << '\n';
 }
}
