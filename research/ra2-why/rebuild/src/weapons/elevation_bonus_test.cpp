#include "cannon_range.h"
#include <iostream>
int main() {
 int source,target,divisor,a,b;double factor,cap;
 while (std::cin >> source >> target >> divisor >> factor >> cap >> a >> b) {
  auto bonus=ra2::weapons::base_arcing_elevation_bonus(bool(a),bool(b),source,target,divisor,factor,cap);
  if (!bonus) return 1;
  std::cout << *bonus << '\n';
 }
}
