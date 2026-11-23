#include "cannon_range.h"
#include <iostream>
int main() {
 double value;int fallback;
 while (std::cin >> value >> fallback) {
  auto result=ra2::weapons::base_weapon_range_from_cells(value,fallback);
  if (!result) return 1;
  std::cout << *result << '\n';
 }
}
